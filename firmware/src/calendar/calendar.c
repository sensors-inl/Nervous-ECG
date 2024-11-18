/**
 *******************************************************************************
 * @file    calendar.c
 * @author  Bertrand Massot (bertrand.massot@insa-lyon.fr)
 * @date    2022-02-24
 * @brief   Calendar module source file
 *******************************************************************************
 */

/*******************************************************************************
 * INCLUDES
 ******************************************************************************/

/* C Standard Library includes */
#include <stdint.h>
#include <time.h>

/* Zephyr Projet includes */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/counter.h>

/* Application includes */
#include "calendar.h"

/*******************************************************************************
 * EXTERN VARIABLES
 ******************************************************************************/

/*******************************************************************************
 * PRIVATE MACROS AND DEFINES
 ******************************************************************************/
#define LOG_MODULE_NAME calendar
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#define TIMER DT_LABEL(DT_NODELABEL(rtc2))

#define ALARM_CHANNEL_ID (0)

#define RTC_PRESCALER (15)                                /**< Power of 2 for RTC frequency (15 is 32768 Hz) - TO BE SET ACCORDING TO PRESCALER IN DTS FILE */
#define RTC_FREQUENCY (1 << RTC_PRESCALER)                /**< RTC counter frequency in Hz (8 - 32768) */
#define COUNTER_TO_SECS(counts) (counts >> RTC_PRESCALER) /**< Macro to convert RTC counter value in seconds */
#define SECS_TO_COUNTER(secs) (secs << RTC_PRESCALER)     /**< Macro to convert seconds in RTC counter value */

/*******************************************************************************
 * PRIVATE TYPEDEFS
 ******************************************************************************/

/*******************************************************************************
 * STATIC VARIABLES
 ******************************************************************************/

static const struct device *counter_dev = DEVICE_DT_GET(DT_NODELABEL(rtc2));
static struct counter_top_cfg top_cfg;
static struct counter_config_info config_info;

static uint64_t time_offset = 0; /**< UNIX 64 bit epoch in seconds - Current time is time_offset + (RTC counter value converted in seconds) */
static uint32_t tick_offset = 0; /**< Microsecs offset set at initial time */

/*******************************************************************************
 * GLOBAL VARIABLES
 ******************************************************************************/

/*******************************************************************************
 * STATIC FUNCTION PROTOTYPES
 ******************************************************************************/

/* RTC Hardware Abstraction Layer */

static void rtc_init(void);
static void rtc_deinit(void);
static void rtc_set_time(const uint64_t timestamp, const uint32_t us);
static void rtc_get_time(uint64_t * p_timestamp, uint32_t * p_us);

static void counter_top_value_cb(const struct device *dev, void *user_data);

/*******************************************************************************
 * GLOBAL FUNCTIONS
 ******************************************************************************/

/**
 * @brief Initialize the calendar
 */
void CAL_Init(void)
{
    rtc_init();
    rtc_set_time(0ULL, 0UL);
}

/**
 * @brief Release the calendar
 */
void CAL_Deinit(void)
{
    rtc_deinit();
}

/**
 * @brief Update calendar time with Unix timestamp
 * @param[in] timestamp containing a Unix epoch
 */
void CAL_SetTime(const uint64_t timestamp, const uint32_t us)
{
    rtc_set_time(timestamp, us);
    LOG_INF("Time set to %llu UTC", timestamp);
}

/**
 * @brief Return current calendar time with Unix timestamp
 * @param[out] timestamp a pointer provided to store the Unix epoch read
 */
void CAL_GetTime(uint64_t *p_timestamp, uint32_t *p_us)
{
    rtc_get_time(p_timestamp, p_us);
}

/*******************************************************************************
 * STATIC FUNCTIONS
 ******************************************************************************/

/* RTC Hardware Abstraction Layer */

static void rtc_init(void)
{
    int err;

    if (counter_dev == NULL)
    {
        LOG_ERR("counter device not found");
        return;
    }

    config_info.freq = counter_get_frequency(counter_dev);
    config_info.max_top_value = counter_get_max_top_value(counter_dev);
    config_info.channels = counter_get_num_of_channels(counter_dev);
    config_info.flags = 0;

    top_cfg.callback = counter_top_value_cb;
    top_cfg.flags = 0;
    top_cfg.ticks = config_info.max_top_value;
    top_cfg.user_data = NULL;

    err = counter_set_top_value(counter_dev, &top_cfg);
    if (err != 0)
    {
        LOG_ERR("setting counter top value failed (code %d)", err);
        return;
    }

    err = counter_start(counter_dev);
    if (err != 0)
    {
        LOG_ERR("starting counter failed (code %d)", err);
        return;
    }

    LOG_INF("Calendar started");
}

static void rtc_deinit(void)
{
    int err;

    if (counter_dev == NULL)
    {
        LOG_ERR("calendar counter does not exist");
        return;
    }

    err = counter_stop(counter_dev);
    if (err != 0)
    {
        LOG_ERR("stopping counter failed (code (%d)", err);
    }
}

static void rtc_set_time(const uint64_t timestamp, const uint32_t us)
{
    int err;

    if (counter_dev == NULL)
    {
        LOG_ERR("calendar counter does not exist");
        return;
    }

    /* Reset counter to zero by setting top value */
    err = counter_set_top_value(counter_dev, &top_cfg);
    if (err != 0)
    {
        LOG_ERR("setting counter top value failed (code %d)", err);
        return;
    }

    time_offset = timestamp;

    float usecs = (float)(us);
    float freq  = (float)(RTC_FREQUENCY);
    tick_offset = (uint32_t)(usecs* freq * 0.000001f);
}

static void rtc_get_time(uint64_t * p_timestamp, uint32_t * p_us)
{
    int err;

    if (counter_dev == NULL)
    {
        LOG_ERR("calendar counter does not exist");
        *p_timestamp = time_offset;
    }

    uint32_t counter_value;
    err = counter_get_value(counter_dev, &counter_value);
    if (err != 0)
    {
        LOG_ERR("reading counter failed (code %d)", err);
        *p_timestamp = time_offset;
    }
    /* Add the original tick offset for microseconds */
    counter_value += tick_offset;
    uint32_t sec = COUNTER_TO_SECS(counter_value);
    *p_timestamp = time_offset + sec;
    uint64_t mod = counter_value - SECS_TO_COUNTER(sec);
    *p_us = (uint32_t)(mod * 1000000ULL / RTC_FREQUENCY);
}

static void counter_top_value_cb(const struct device *dev, void *user_data)
{
    uint32_t time = COUNTER_TO_SECS((config_info.max_top_value + 1));
    time_offset += time;
    LOG_INF("counter loop %u ticks is %u secs", config_info.max_top_value, time);
}
