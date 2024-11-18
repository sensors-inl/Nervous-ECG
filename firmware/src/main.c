/**
 *******************************************************************************
 * @file    main.c
 * @author  Bertrand Massot (bertrand.massot@insa-lyon.fr)
 * @date    2022-02-24
 * @brief   RECAMED ECG project main file
 *******************************************************************************
 */

/*******************************************************************************
 * INCLUDES
 ******************************************************************************/

/* C Standard Library includes */
#include <stdio.h>

/* Zephyr Projet includes */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/bluetooth/services/bas.h>
#include <pb_decode.h>

/* Application includes */
#include "bluetooth/bluetooth.h"
#include "calendar/calendar.h"
#include "measurement.h"
#include "nanocobs/cobs.h"

/* Generated headers */
#include "protocol/protocol.pb.h"
#include "app_version.h"

/*******************************************************************************
 * EXTERN VARIABLES
 ******************************************************************************/

/*******************************************************************************
 * PRIVATE MACROS AND DEFINES
 ******************************************************************************/

#define LOG_MODULE_NAME main
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#define MEASUREMENT_THREAD_PRIORITY	    -1

#define RGB_LED_BLINK_PERIOD            500     // ms
#define RUN_SLEEP_INTERVAL              60000   // ms

/*******************************************************************************
 * PRIVATE TYPEDEFS
 ******************************************************************************/

/* Application states enumeration */
typedef enum
{
    APP_STATE_MEASURING,
    NUM_OF_APP_STATES,
} app_state_t;

/* USB connection state */
typedef enum
{
    USB_STATE_DISCONNECTED,
    USB_STATE_CHARGING,
    USB_STATE_CHARGE_DONE,
    NUM_OF_USB_STATES,
} usb_state_t;

/*******************************************************************************
 * GLOBAL VARIABLES
 ******************************************************************************/

K_THREAD_STACK_DEFINE(measurement_stack_area, 512);
struct k_thread measurement_thread;
k_tid_t measurement_thread_tid;

K_THREAD_STACK_DEFINE(status_stack_area, 512);
struct k_thread status_thread;

/*******************************************************************************
 * STATIC FUNCTION PROTOTYPES
 ******************************************************************************/

/* Measurement thread control function (called async in working queue)*/
static void measurement_start(struct k_work * work);
static void measurement_stop (struct k_work * work);

/* BLE connection events */
static void ble_evt_callback(ble_event_type_t event);
/* BLE NUS data events */
static void ble_rx_callback(const uint8_t *const p_data, uint16_t length);

/* Fuel gauge helper */
static int32_t get_state_of_charge(const struct device *dev);

/* RGB LEd helpers */
static void rgb_led_init(void);
static void rgb_led_set(bool red, bool green, bool blue);
static void rgb_led_blink_blue(void);
static void rgb_led_timer_handler(struct k_timer *timer);

/*******************************************************************************
 * STATIC VARIABLES
 ******************************************************************************/

/* Start and stop work tasks */
K_WORK_DEFINE(start_measure, measurement_start);
K_WORK_DEFINE(stop_measure,  measurement_stop);

/* RGB led timer for blinking */
K_TIMER_DEFINE(rgb_led_timer, rgb_led_timer_handler, NULL);

/* RGB Led pins */
static const struct gpio_dt_spec led_red_pin    = GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), led_rgb_gpios, 0);
static const struct gpio_dt_spec led_green_pin  = GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), led_rgb_gpios, 1);
static const struct gpio_dt_spec led_blue_pin   = GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), led_rgb_gpios, 2);

/* Fuel gauge device */
static const struct device *fuel_gauge = DEVICE_DT_GET(DT_NODELABEL(bq27441));

/* Store global application state */
static app_state_t m_app_state;

/* Message reception buffers */
static uint8_t message[Timestamp_size + 1];
static Timestamp timestamp;

/*******************************************************************************
 * GLOBAL FUNCTIONS
 ******************************************************************************/
int main(void)
{
    printk("\n");
    printk("ECG Firmware version %s\n", APP_VERSION_STRING);

    /* Initialize RGB led pins and set white color to inform about boot */
    rgb_led_init();
    rgb_led_set(true, true, true);

    CAL_Init();

	/* Start Bluetooth stack */
	BLE_Init();
    BLE_SetEventCallback(ble_evt_callback);
    BLE_SetReceiveCallback(ble_rx_callback);

    /* Initialize and set frontend in shutdown */
    MEAS_Init();
    /* Create thread which will be internally paused immediately */
	measurement_thread_tid = k_thread_create(&measurement_thread, measurement_stack_area, 
									K_THREAD_STACK_SIZEOF(measurement_stack_area), 
									MEAS_Thread, NULL, NULL, NULL, 
									MEASUREMENT_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(measurement_thread_tid, "Measurement");

    /* Start advertising */
	BLE_StartAdvertising();
    rgb_led_blink_blue();

    printk("\n*** Application started ***\n\n");

	for (;;) {

        /* Periodically retrieve battery state */
        int battery = get_state_of_charge(fuel_gauge);
        bt_bas_set_battery_level(battery);  
        if (battery < 20) {
            rgb_led_set(true, false, false);
        }
        
        k_sleep(K_MSEC(RUN_SLEEP_INTERVAL));

	}
	
}


/*******************************************************************************
 * STATIC FUNCTIONS
 ******************************************************************************/

/* Start impedance measurement using current profile */
void measurement_start(struct k_work * work)
{
    LOG_INF("%s", "Start measurement thread");
    m_app_state = APP_STATE_MEASURING;
    MEAS_Enable(true);
    k_thread_resume(measurement_thread_tid);
}

/* Stop immediateley any measurement in progress */
void measurement_stop(struct k_work * work)
{
    LOG_INF("%s", "Abort measurement thread");
	k_thread_suspend(measurement_thread_tid);
    MEAS_Enable(false);
}

static int32_t get_state_of_charge(const struct device *dev) {
    int status = 0;
    struct sensor_value state_of_charge, avg_current, voltage;

    status = sensor_sample_fetch_chan(dev, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE);
    if (status < 0) {
        LOG_ERR("Unable to fetch state of charge");
        return -1;
    }

    status = sensor_channel_get(dev, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE, &state_of_charge);
    if (status < 0) {
        LOG_ERR("Unable to get state of charge");
        return -1;
    }
    
    status = sensor_sample_fetch_chan(dev, SENSOR_CHAN_GAUGE_AVG_CURRENT);
    if (status < 0) {
        LOG_ERR("Unable to fetch avg current");
        return -1;
    }

    status = sensor_channel_get(dev, SENSOR_CHAN_GAUGE_AVG_CURRENT, &avg_current);
    if (status < 0) {
        LOG_ERR("Unable to get avg current");
        return -1;
    }
    
    status = sensor_sample_fetch_chan(dev, SENSOR_CHAN_GAUGE_VOLTAGE);
    if (status < 0) {
        LOG_ERR("Unable to fetch voltage");
        return -1;
    }

    status = sensor_channel_get(dev, SENSOR_CHAN_GAUGE_VOLTAGE, &voltage);
    if (status < 0) {
        LOG_ERR("Unable to get voltage");
        return -1;
    }

    LOG_INF("State of charge: %d%%, current: %d mA, voltage: %d mV", state_of_charge.val1, avg_current.val1 * 1000 + avg_current.val2 / 1000, voltage.val1 * 1000 + voltage.val2 / 1000);

    return state_of_charge.val1;
}

/* Bluetooth events callback */
void ble_evt_callback(ble_event_type_t event)
{
    switch(event)
    {
        case BLE_EVT_CONNECTED:
            k_work_submit(&start_measure);
            rgb_led_set(false, false, true);
            LOG_INF("BLE connected");
            break;
        case BLE_EVT_DISCONNECTED:
            k_work_submit(&stop_measure);
            rgb_led_blink_blue();
            LOG_INF("BLE disconnected");
            break;
        case BLE_EVT_NUS_ENABLED:
            rgb_led_set(false, true, false);
            LOG_INF("BLE NUS notifications enabled");
            break;
        case BLE_EVT_NUS_DISABLED:
            rgb_led_set(false, false, true);
            LOG_INF("BLE NUS notifications disabled");
            break;
    }
}

/* BLE NUS data received */
static void ble_rx_callback(const uint8_t *const p_data, uint16_t length)
{
    if (length > sizeof(message))
    {
        LOG_ERR("Size of message is %u but max is %u", length, Timestamp_size + 1);
        return;
    }
    memcpy(message, p_data, length);
    cobs_ret_t cobs_ret = cobs_decode_inplace(message, length);
    if (cobs_ret != COBS_RET_SUCCESS)
    {
        LOG_ERR("error %d while decoding cobs", cobs_ret);
        return;
    }

    /* Decode protobuf message (should be a request) */
    pb_istream_t istream = pb_istream_from_buffer(message + 1, length - 2);
    bool status = pb_decode(&istream, Timestamp_fields, &timestamp);
    if (!status) {
        LOG_ERR("protobuf decoding failed: %s\n", PB_GET_ERROR(&istream));
        return;
    }

    CAL_SetTime(timestamp.time, timestamp.us);
}

/* Init RGB gpios */
static void rgb_led_init(void)
{
    gpio_pin_configure_dt(&led_red_pin,   GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led_green_pin, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led_blue_pin,  GPIO_OUTPUT_INACTIVE);
}

/* Set RGB state */
static void rgb_led_set(bool red, bool green , bool blue)
{
    /* if led was blinking we need to be sure that timer is stopped */
    k_timer_stop(&rgb_led_timer);
    gpio_pin_set_dt(&led_red_pin,   red);
    gpio_pin_set_dt(&led_green_pin, green);
    gpio_pin_set_dt(&led_blue_pin,  blue);
}

/* Start the RGB led blinking timer */
static void rgb_led_blink_blue(void)
{
    rgb_led_set(false, false, true);
    k_timer_start(&rgb_led_timer, K_MSEC(RGB_LED_BLINK_PERIOD), K_MSEC(RGB_LED_BLINK_PERIOD));
}

/* RGB led blinking timer callback */
static void rgb_led_timer_handler(struct k_timer *timer)
{
    gpio_pin_toggle_dt(&led_blue_pin);
}
