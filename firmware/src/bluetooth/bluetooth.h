/**
 *******************************************************************************
 * @file    bluetooth.h
 * @author  Bertrand Massot (bertrand.massot@insa-lyon.fr)
 * @date    2022-02-24
 * @brief   Bluetooth module header file
 *******************************************************************************
 */

#ifndef __BLUETOOTH_H__
#define __BLUETOOTH_H__

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * INCLUDES
 ******************************************************************************/

#include <stdint.h>
#include <stdbool.h>

/*******************************************************************************
 * MACROS AND DEFINES
 ******************************************************************************/

#define BLE_RX_MAX_BUFFER_SIZE      255             /**< Maximum payload from BLE messages received */
#define BLE_TX_MAX_BUFFER_SIZE      255             /**< Maximum payload for BLE messages sent */

/*******************************************************************************
 * TYPEDEFS
 ******************************************************************************/

/* Bluetooth connection event type */
typedef enum
{
    BLE_EVT_CONNECTED = 0,
    BLE_EVT_DISCONNECTED,
    BLE_EVT_NUS_ENABLED,
    BLE_EVT_NUS_DISABLED
} ble_event_type_t;

/* Requests / commands /messages codes enumeration */
typedef enum
{
    BLE_REQ_START_MEASURE = 0,
    BLE_REQ_STOP_MEASURE = 1,
    BLE_MSG_MEASURE_DATA = 2,
    NUM_OF_BLE_MSG_TYPES,
} ble_packet_type_t;

/* BLE messages structure type */
typedef struct
{
    uint16_t type;
    uint16_t length;    // this is data length, not full packet length (full length = data length + 4)
    uint8_t  data[];    // this should be aligned, so a pointer can be used
} ble_rx_packet_t;

typedef struct
{
    uint16_t type;
    uint16_t length;    // this is data length, not full packet length (full length = data length + 4)
    uint8_t  data[BLE_TX_MAX_BUFFER_SIZE];
} ble_tx_packet_t;

/* BLE user callback types */
typedef void (*BLE_EventCallback_t)(ble_event_type_t event);
typedef void (*BLE_ReceiveCallback_t)(const uint8_t *const p_data, 
                                      uint16_t length);

/*******************************************************************************
 * EXPORTED VARIABLES
 ******************************************************************************/

/*******************************************************************************
 * GLOBAL FUNCTION PROTOTYPES
 ******************************************************************************/

int  BLE_Init(void);
void BLE_StartAdvertising(void);
void BLE_StopAdvertising(void);
bool BLE_IsConnected(void);
bool BLE_IsSendEnabled(void);
void BLE_Disconnect(void);
void BLE_Send(uint8_t * p_data, uint16_t length);
void BLE_SetReceiveCallback(BLE_ReceiveCallback_t receive_callback);
void BLE_SetEventCallback(BLE_EventCallback_t event_callback);

#ifdef __cplusplus
}
#endif

#endif /* __BLUETOOTH_H__ */