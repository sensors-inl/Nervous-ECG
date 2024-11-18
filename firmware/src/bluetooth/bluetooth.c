/**
 *******************************************************************************
 * @file    bluetooth.c
 * @author  Bertrand Massot (bertrand.massot@insa-lyon.fr)
 * @date    2022-02-24
 * @brief   Bluetooth module source file
 *******************************************************************************
 */

/*******************************************************************************
 * INCLUDES
 ******************************************************************************/

/* C Standard Library includes */
#include <stdint.h>
#include <stdio.h>

/* Zephyr Projet includes */
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/bluetooth/hci_driver.h>
#include <bluetooth/services/nus.h>
#include <zephyr/settings/settings.h>

/* Application includes */
#include "bluetooth.h"

/* Generating includes */
#include "app_version.h"

/*******************************************************************************
 * EXTERN VARIABLES
 ******************************************************************************/

/*******************************************************************************
 * PRIVATE MACROS AND DEFINES
 ******************************************************************************/
#define LOG_MODULE_NAME bluetooth
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN	(sizeof(DEVICE_NAME) - 1)

/*******************************************************************************
 * PRIVATE TYPEDEFS
 ******************************************************************************/

/*******************************************************************************
 * STATIC VARIABLES
 ******************************************************************************/

static struct bt_data _ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static char device_name[DEVICE_NAME_LEN+10] = {0};

static const struct bt_data _sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_VAL),
};

static struct bt_conn *_conn = NULL;
static enum bt_nus_send_status _nus_send_status = BT_NUS_SEND_STATUS_DISABLED;

static BLE_ReceiveCallback_t _receive_callback = NULL;
static BLE_EventCallback_t _event_callback = NULL;

static uint8_t * _tx_buffer;
static uint32_t _bytes_to_send;
static uint32_t mtu_size;


/*******************************************************************************
 * GLOBAL VARIABLES
 ******************************************************************************/

/*******************************************************************************
 * STATIC FUNCTION PROTOTYPES
 ******************************************************************************/
static void ble_connected(struct bt_conn *conn, uint8_t err);
static void ble_disconnected(struct bt_conn *conn, uint8_t reason);

static bool le_param_req(struct bt_conn *conn, struct bt_le_conn_param *param);
static void le_param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency, uint16_t timeout);
static void le_phy_updated(struct bt_conn *conn, struct bt_conn_le_phy_info *param);
static void le_data_length_updated(struct bt_conn *conn, struct bt_conn_le_data_len_info *info);

static void nus_receive_callback(struct bt_conn *conn, 
                                  const uint8_t *const data, uint16_t len);
static void nus_sent_callback(struct bt_conn *conn);
static void nus_send_enabled_callback(enum bt_nus_send_status status);
static void nus_send_next_packet(void);

/* This should be declared upper but needs static function prototypes */
static struct bt_conn_cb _conn_cb = {
	    .connected = ble_connected,
	    .disconnected = ble_disconnected,
	    .le_param_req = le_param_req,
	    .le_param_updated = le_param_updated,
	    .le_phy_updated = le_phy_updated,
	    .le_data_len_updated = le_data_length_updated
};

static struct bt_nus_cb _nus_cb = {
	.received     = nus_receive_callback,
    .sent         = nus_sent_callback,
    .send_enabled = nus_send_enabled_callback,
};

/*******************************************************************************
 * GLOBAL FUNCTIONS
 ******************************************************************************/

int BLE_Init(void)
{
	int err = 0;

    bt_conn_cb_register(&_conn_cb);

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Failed to initialize Bluetooth (err: %d)", err);
    return err;
	}

	LOG_INF("%s", "Bluetooth initialized");

	/* Must be called after enabling Bluetooth stack */
	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

    /* Set firmware version string */
    char fw_str[32];
    int len = snprintf(fw_str, sizeof(fw_str), "%u.%u.%u", APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_PATCHLEVEL);
    settings_runtime_set("bt/dis/fw", fw_str, len);


	err = bt_nus_init(&_nus_cb);
	if (err) {
		LOG_ERR("Failed to initialize UART service (err: %d)", err);
	}

    /* Automatically add 2 MSB of MAC address to the name */
    /*bt_addr_le_t addrs[CONFIG_BT_ID_MAX] = {0};
    size_t count = 0;
    bt_id_get(addrs, &count);*/
    struct bt_hci_vs_static_addr hci_addr;
    bt_read_static_addr(&hci_addr, 1);

    sprintf(device_name, "%s %02X%02X", DEVICE_NAME, hci_addr.bdaddr.val[1], hci_addr.bdaddr.val[0]);
    err = bt_set_name(device_name);
	if (err) {
		LOG_ERR("Failed to set BLE device name (err: %d)", err);
	}
    _ad[1].type = BT_DATA_NAME_COMPLETE;
    _ad[1].data_len = strlen(device_name);
    _ad[1].data = device_name;

    return err;
}


void BLE_StartAdvertising(void)
{
	int err = 0;

	err = bt_le_adv_start(BT_LE_ADV_CONN, _ad, ARRAY_SIZE(_ad), _sd, ARRAY_SIZE(_sd));

	if (err) {
		LOG_ERR("Advertising failed to start (err %d)", err);
	}
}


void BLE_StopAdvertising(void)
{
    int err = 0;

    err = bt_le_adv_stop();
	if (err) {
		LOG_ERR("Failed to stop advertising (err %d)", err);
	}
}


bool BLE_IsConnected(void)
{
    return !(_conn == NULL);
}


bool BLE_IsSendEnabled(void)
{
    return (_nus_send_status == BT_NUS_SEND_STATUS_ENABLED);
}


void BLE_Disconnect(void)
{
    int err = 0;

    if (_conn == NULL) {
        LOG_ERR("%s", "Failed to disconnect a non-existing connection");
        return;
    }

    err = bt_conn_disconnect(_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    if (err) {
        LOG_ERR("Failed to disconnect (err %d)", err);
    }
    
}


void BLE_Send(uint8_t * p_data, uint16_t length)
{
    if (_conn == NULL) {
        LOG_ERR("%s", "No connection to send NUS data");
        return;
    }

    if (_nus_send_status == BT_NUS_SEND_STATUS_DISABLED) {
        LOG_ERR("%s", "NUS notifications not enabled");
        return;
    }

    _tx_buffer     = p_data;
    _bytes_to_send = length;
    mtu_size = bt_nus_get_mtu(_conn);
    LOG_DBG("MTU %u", mtu_size);
    nus_send_next_packet();
}


void BLE_SetReceiveCallback(BLE_ReceiveCallback_t receive_callback)
{
    _receive_callback = receive_callback;
}


void BLE_SetEventCallback(BLE_EventCallback_t event_callback)
{
    _event_callback = event_callback;
}

/*******************************************************************************
 * STATIC FUNCTIONS
 ******************************************************************************/

static const char *phy2str(uint8_t phy)
{
	switch (phy) {
	case 0: return "No packets";
	case BT_GAP_LE_PHY_1M: return "LE 1M";
	case BT_GAP_LE_PHY_2M: return "LE 2M";
	case BT_GAP_LE_PHY_CODED: return "LE Coded";
	default: return "Unknown";
	}
}

static void ble_connected(struct bt_conn *conn, uint8_t err)
{
	struct bt_conn_info info = {0};
	char addr[BT_ADDR_LE_STR_LEN];

	if (err) {
		LOG_ERR("Connection failed (err %u)", err);
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	_conn = bt_conn_ref(conn);

	err = bt_conn_get_info(_conn, &info);
	if (err) {
		LOG_ERR("Failed to get connection info %d", err);
		return;
	}

    mtu_size = bt_nus_get_mtu(_conn);

    if (_event_callback != NULL) {
        _event_callback(BLE_EVT_CONNECTED);
    }
}

static void ble_disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];
    BLE_StartAdvertising();

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (_conn) {
		bt_conn_unref(_conn);
		_conn = NULL;
	}

    _nus_send_status = BT_NUS_SEND_STATUS_DISABLED;

    if (_event_callback != NULL) {
    _event_callback(BLE_EVT_DISCONNECTED);
    }
}

static bool le_param_req(struct bt_conn *conn, struct bt_le_conn_param *param)
{
	LOG_INF("%s", "Connection parameters update request received.");
	LOG_WRN("Minimum interval: %d, Maximum interval: %d", param->interval_min, param->interval_max);
	LOG_WRN("Latency: %d, Timeout: %d", param->latency, param->timeout);

	return true;
}

static void le_param_updated(struct bt_conn *conn, uint16_t interval,
			     uint16_t latency, uint16_t timeout)
{
	LOG_INF("%s", "Connection parameters updated.");
    LOG_WRN("Interval: %d, latency: %d, timeout: %d", interval, latency, timeout);
}

static void le_phy_updated(struct bt_conn *conn,
			   struct bt_conn_le_phy_info *param)
{
	LOG_WRN("LE PHY updated: TX PHY %s, RX PHY %s",
	       phy2str(param->tx_phy), phy2str(param->rx_phy));
}

static void le_data_length_updated(struct bt_conn *conn,
				   struct bt_conn_le_data_len_info *info)
{
	LOG_WRN("LE data len updated: TX (len: %d time: %d)"
	       " RX (len: %d time: %d)", info->tx_max_len,
	       info->tx_max_time, info->rx_max_len, info->rx_max_time);
    mtu_size = bt_nus_get_mtu(conn);
}

static void nus_receive_callback(struct bt_conn *conn, 
                                 const uint8_t *const data, uint16_t len)
{
	char addr[BT_ADDR_LE_STR_LEN] = {0};

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, ARRAY_SIZE(addr));

    if (_receive_callback != NULL) {
        _receive_callback(data, len);
    }
}

static void nus_sent_callback(struct bt_conn *conn)
{
    if (_bytes_to_send != 0) {
        nus_send_next_packet();
    }
}

static void nus_send_enabled_callback(enum bt_nus_send_status status)
{
    if (status == BT_NUS_SEND_STATUS_ENABLED) {
        if (_event_callback != NULL) {
            _event_callback(BLE_EVT_NUS_ENABLED);
        }
    } else if (status == BT_NUS_SEND_STATUS_DISABLED) {
        if (_event_callback != NULL) {
            _event_callback(BLE_EVT_NUS_DISABLED);
        }
    }
    _nus_send_status = status;
}

static void nus_send_next_packet(void)
{
    int err = 0;
  
    mtu_size = bt_nus_get_mtu(_conn);
    if (mtu_size == 0) {
        LOG_ERR("%s", "Failed to send NUS data (mtu size is 0)");
        _bytes_to_send = 0;
        return;
    }

    if (_bytes_to_send <= mtu_size) {
        mtu_size = _bytes_to_send;
    }

    err = bt_nus_send(_conn, _tx_buffer, mtu_size);
    if (err) {
        LOG_ERR("Failed to send NUS data (err %d)", err);
        _bytes_to_send = 0;
    return;
	}

    _bytes_to_send -= mtu_size;
    _tx_buffer     += mtu_size;
}