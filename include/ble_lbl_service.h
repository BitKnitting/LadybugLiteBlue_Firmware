/* Copyright (c) 2015 Margaret Johnson
 *
 */

#ifndef BLE_LBL_H__
#define BLE_LBL_H__

#include <stdint.h>
#include <stdbool.h>
#include "ble.h"
#include "ble_srv_common.h"

/*!
 * \brief created a UUID using uuidgen when in a terminal (mac).  left the last 32 bits 0 - similar to Nordic's nAN-36_v1.1.pdf (app note on writing a BLE app) - p. 21.
 * The UUID gets added to the softdevice's list of UUIDs by calling sd_ble_uuid_vs_add()
 */
#define LBL_UUID_BASE {0x8E, 0xDF, 0x91, 0xBA, 0x1E, 0x09, 0x47, 0x44, 0xBE, 0x03, 0x78, 0x01, 0x00, 0x00, 0x00, 0x00}
/*!
 * \brief hex value for the LBL BLE Service UUID that gets added to the softdevice through a call to sd_ble_gatts_service_add(), associated with the LBL_UUID_BASE UUID
 */
#define LBL_UUID_SERVICE 0x8E00
#define LBL_UUID_BATT_CHAR 0x8E01
//#define LBL_UUID_HYDRO_CHAR 0x8E02  NO LONGER USING
#define LBL_UUID_CONTROL_CHAR 0x8E03
#define LBL_UUID_PLANTINFO_CHAR 0x8E04
#define LBL_UUID_MEASUREMENT_CHAR 0x8E05
#define LBL_UUID_CALIBRATION_CHAR 0x8E06

/**@brief LBL Service structure. This contains various status information for the service. */
typedef struct ble_lbl_s
{
    uint16_t                    service_handle;
    ble_gatts_char_handles_t    batt_char_handles;
    ble_gatts_char_handles_t    hydro_char_handles;
    ble_gatts_char_handles_t	plantInfo_char_handles;
    ble_gatts_char_handles_t	control_char_handles;
    ble_gatts_char_handles_t	measurement_char_handles;
    ble_gatts_char_handles_t	calibration_char_handles;
    uint8_t                     uuid_type;
    uint16_t                    conn_handle;
} ble_lbl_t;

/**@brief Function for initializing the LBL Service.
 *
 * @param[out]  p_lbl       LBL Service structure. This structure will have to be supplied by
 *                          the application. It will be initialized by this function, and will later
 *                          be used to identify this particular service instance.
 *
 * @return      NRF_SUCCESS on successful initialization of service, otherwise an error code.
 */
uint32_t ble_lbl_init(ble_lbl_t * p_lbl);

/**@brief Function for handling the Application's BLE Stack events.
 *
 * @details Handles all events from the BLE stack of interest to the LBL Service.
 *
 *
 * @param[in]   p_lbl      LBL Service structure.
 * @param[in]   p_ble_evt  Event received from the BLE stack.
 */
void ble_lbl_on_ble_evt(ble_lbl_t * p_lbl, ble_evt_t * p_ble_evt);


#endif // BLE_LBL_H__

/** @} */
