/**
 *******************************************************************************
 * @file    measurement.c
 * @author  Bertrand Massot (bertrand.massot@insa-lyon.fr)
 * @date    2022-03-29
 * @brief   Measurement thread module source file
 *******************************************************************************
 */

/*******************************************************************************
 * INCLUDES
 ******************************************************************************/

/* C Standard Library includes */
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* Zephyr Projet includes */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <bluetooth/services/nus.h>
#include <pb_encode.h>

/* Application includes */
#include "bluetooth/bluetooth.h"
#include "calendar/calendar.h"
#include "protocol/protocol.pb.h"
#include "nanocobs/cobs.h"
#include "measurement.h"


/*******************************************************************************
 * EXTERN VARIABLES
 ******************************************************************************/

/*******************************************************************************
 * PRIVATE MACROS AND DEFINES
 ******************************************************************************/

#define LOG_MODULE_NAME measurement
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#define ADC_SAMPLE_NUM (sizeof(((EcgBuffer *)0)->data))/2 /**< Number of samples acquired per buffer */
#define ADC_SAMPLE_INTERVAL     1953  /**< microseconds (= 1/512) */
/* The following value was experimentally adjusted */
#define ADC_BUFF_INTERVAL       (ADC_SAMPLE_INTERVAL - 368) /**< wait the necessary time (should be one sampling interval) before re-starting buffer acquisition */
/*******************************************************************************
 * PRIVATE TYPEDEFS
 ******************************************************************************/

/*******************************************************************************
 * STATIC VARIABLES
 ******************************************************************************/

/* Buffer timestamp */
static uint64_t timestamp;
static uint32_t us;

/* Protobuf stream buffer */
static pb_byte_t proto_buffer[EcgBuffer_size + 2]; // COBS needs one byte at start and one byte at end to store zero positions

/* ECG Data double buffering */
static int16_t buffer_1[ADC_SAMPLE_NUM];
static int16_t buffer_2[ADC_SAMPLE_NUM];
static int16_t * buffer_to_send = buffer_2;
static EcgBuffer ecgBuffer = {
    .data = {0},
    .lodpn = 0UL,
    .has_timestamp = true,
    .timestamp = {
        .time = 0ULL,
        .us = 0UL
    }
};

/* Lead off detection status variable */
static uint16_t lodpn;

/* AD8232 I/O pins */
const static struct gpio_dt_spec ad8232_pwr_pin_dt   = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), ad8232_pwr_gpios);
const static struct gpio_dt_spec ad8232_lodp_pin_dt  = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), ad8232_lodp_gpios);
const static struct gpio_dt_spec ad8232_lodn_pin_dt  = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), ad8232_lodn_gpios);

/* ADC Configuration */
const static struct device * adc_dev = DEVICE_DT_GET(DT_IO_CHANNELS_CTLR_BY_NAME(DT_PATH(zephyr_user), ad8232_out));
const static uint8_t ad8232_out_ch = DT_IO_CHANNELS_INPUT_BY_NAME(DT_PATH(zephyr_user), ad8232_out);
const static uint8_t ad8232_ref_ch = DT_IO_CHANNELS_INPUT_BY_NAME(DT_PATH(zephyr_user), ad8232_ref);

const struct adc_channel_cfg channel_cfg = {
    .acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 40),
    .differential = true,
    .gain = ADC_GAIN_1,
    .reference = ADC_REF_VDD_1_4,
    .channel_id = 0,
    .input_positive = ad8232_out_ch, // AIN1 is P0.03
    .input_negative = ad8232_ref_ch,
};

struct adc_sequence_options sequence_options = {
    .callback = NULL,
    .extra_samplings = ADC_SAMPLE_NUM - 1,
    .interval_us = ADC_SAMPLE_INTERVAL,
    .user_data = NULL,
};

struct adc_sequence sequence = {
    .buffer = buffer_1,
    .buffer_size = sizeof(buffer_1),
    .calibrate = false,
    .channels = BIT(0),
    .oversampling = 2,
    .resolution = 14,
    .options = &sequence_options,
};

/*******************************************************************************
 * STATIC FUNCTION PROTOTYPES
 ******************************************************************************/

/* Wake up system from sleep state */
void send_buffer(struct k_work *work);
K_WORK_DEFINE(ble_send, send_buffer);

/*******************************************************************************
 * GLOBAL FUNCTIONS
 ******************************************************************************/

void MEAS_Init(void)
{
	int err;

    /* Configure shutdown pin */
	err = gpio_pin_configure_dt(&ad8232_pwr_pin_dt, GPIO_OUTPUT_INACTIVE);
    if (err != 0) {
        LOG_ERR("failed configure ad8232 power pin (code %d)", err);
    }

    /* Configure lead-off detection pins */
    err = gpio_pin_configure_dt(&ad8232_lodp_pin_dt, GPIO_INPUT);
    if (err != 0) {
        LOG_ERR("failed configure ad8232 LOD+ pin (code %d)", err);
    }

    err = gpio_pin_configure_dt(&ad8232_lodn_pin_dt, GPIO_INPUT);
    if (err != 0) {
        LOG_ERR("failed configure ad8232 LOD- pin (code %d)", err);
    }

	if (!device_is_ready(adc_dev)) {
		LOG_ERR("ADC device is not ready %s", adc_dev->name);
	}
}


void MEAS_Enable(bool enable)
{
    int err;
    if (enable)
    {
        /* Power up AD8232 */
        err = gpio_pin_set_dt(&ad8232_pwr_pin_dt, 1);
        if (err != 0) {
        LOG_ERR("failed to set ad5940 power pin (code %d)", err);
        }
    }
    else {
        /* Power down AD8232 */
        err = gpio_pin_set_dt(&ad8232_pwr_pin_dt, 0);
        if (err != 0) {
         LOG_ERR("failed to clear ad5940 power pin (code %d)", err);
        }
    }
}

void MEAS_Read(void)
{
	int err;
    lodpn =  gpio_pin_get_dt(&ad8232_lodn_pin_dt) << 1; // RA (0 or 2)
    lodpn += gpio_pin_get_dt(&ad8232_lodp_pin_dt);      // LA (0 or 1)

    /* Set up ADC readings */
    err = adc_channel_setup(adc_dev, &channel_cfg);
    if (err != 0) {
       LOG_ERR("failed to configure adc channel (code %d)", err);
    }

    /* Start acquisition */
    err = adc_read(adc_dev, &sequence);
	if (err != 0) {
		LOG_ERR("failed to acquire adc channel (code %d)", err);
	}

    /* Swap buffers */
    if (sequence.buffer == buffer_1)
    {
        sequence.buffer = buffer_2;
        buffer_to_send = buffer_1;
    }
    else
    {
        sequence.buffer = buffer_1;
        buffer_to_send = buffer_2;
    }

    /* Launch sending task */
    err = k_work_submit(&ble_send);
	if (err < 0) {
		LOG_ERR("failed to launch aync ble sending (code %d)", err);
	}
}

void MEAS_Thread(void *p1, void *p2, void *p3)
{
    //LOG_INF("%s", "Measurement thread created");
    k_thread_suspend(k_current_get());
    while (1)
    {
        CAL_GetTime(&timestamp, &us);
        MEAS_Read();
        /* Add one delay between last sample and first sample of next buffer */
        //k_usleep(ADC_BUFF_INTERVAL);
        k_usleep(ADC_BUFF_INTERVAL);
    }
}

/*******************************************************************************
 * STATIC FUNCTIONS
 ******************************************************************************/
enum bt_nus_send_status status=1;
void send_buffer(struct k_work *work)
{
    if (BLE_IsSendEnabled())
    {
        memcpy(ecgBuffer.data, buffer_to_send, 2 * ADC_SAMPLE_NUM);
        ecgBuffer.lodpn = lodpn;
        ecgBuffer.timestamp.time = timestamp;
        ecgBuffer.timestamp.us = us;
        pb_ostream_t ostream = pb_ostream_from_buffer(proto_buffer + 1, EcgBuffer_size);

        bool pb_ret = pb_encode(&ostream, EcgBuffer_fields, &ecgBuffer);
        if (pb_ret == false) {
            LOG_ERR("Error while encoding protobuf : %s", ostream.errmsg);
             return;
        }
        proto_buffer[0] = COBS_INPLACE_SENTINEL_VALUE;
        proto_buffer[ostream.bytes_written + 1] = COBS_INPLACE_SENTINEL_VALUE;
        //ostream.bytes_written + 2 <= COBS_INPLACE_SAFE_BUFFER_SIZE ?
        cobs_ret_t cobs_ret = cobs_encode_inplace(proto_buffer, ostream.bytes_written + 2);
        // check for cobs_ret value
        if (cobs_ret != COBS_RET_SUCCESS) {
            LOG_ERR("Error while encoding COBS message (err %u)", cobs_ret);
             return;
        }
        BLE_Send((uint8_t *)proto_buffer, ostream.bytes_written + 2);
    }
}