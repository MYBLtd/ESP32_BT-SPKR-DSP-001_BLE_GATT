/*
 * BLE GATT DSP Control Service
 * FSD-DSP-001: DSP Control via BLE GATT
 *
 * Implements Section 10 of the FSD:
 * - DSP_CONTROL service with custom 128-bit UUID
 * - CONTROL_WRITE characteristic (Write, Write Without Response)
 * - STATUS_NOTIFY characteristic (Read, Notify)
 * - 2-byte command protocol
 *
 * Author: Robin Kluit
 * Date: 2026-01-20
 */

#ifndef BLE_GATT_DSP_H
#define BLE_GATT_DSP_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "dsp_processor.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * BLE GATT UUIDs (Section 10.2)
 * Custom 128-bit UUIDs for DSP Control Service
 *
 * Base UUID: xxxxxxxx-1234-5678-9ABC-DEF012345678
 * Service:   DSP_CTRL (0x0001)
 * Control:   CTRL_WR  (0x0002)
 * Status:    STAT_NTF (0x0003)
 */

/* DSP Control Service UUID: 00000001-1234-5678-9ABC-DEF012345678 */
#define DSP_SERVICE_UUID_128 { \
    0x78, 0x56, 0x34, 0x12, 0xF0, 0xDE, 0xBC, 0x9A, \
    0x78, 0x56, 0x34, 0x12, 0x01, 0x00, 0x00, 0x00 \
}

/* Control Write Characteristic UUID: 00000002-1234-5678-9ABC-DEF012345678 */
#define DSP_CONTROL_CHAR_UUID_128 { \
    0x78, 0x56, 0x34, 0x12, 0xF0, 0xDE, 0xBC, 0x9A, \
    0x78, 0x56, 0x34, 0x12, 0x02, 0x00, 0x00, 0x00 \
}

/* Status Notify Characteristic UUID: 00000003-1234-5678-9ABC-DEF012345678 */
#define DSP_STATUS_CHAR_UUID_128 { \
    0x78, 0x56, 0x34, 0x12, 0xF0, 0xDE, 0xBC, 0x9A, \
    0x78, 0x56, 0x34, 0x12, 0x03, 0x00, 0x00, 0x00 \
}

/*
 * Control Protocol (Section 10.3)
 * Format: [CMD (1 byte)] [VAL (1 byte)]
 */
#define DSP_CMD_SET_PRESET      0x01    /* VAL: 0-3 (preset ID) */
#define DSP_CMD_SET_LOUDNESS    0x02    /* VAL: 0/1 (off/on) */
#define DSP_CMD_GET_STATUS      0x03    /* VAL: 0 (triggers notify) */

/*
 * Status Payload (Section 10.4)
 * Format: [VER][PRESET][LOUDNESS][FLAGS]
 */
#define DSP_STATUS_PROTOCOL_VERSION 0x01
#define DSP_STATUS_SIZE             4

/*
 * BLE advertising configuration
 */
#define BLE_DEVICE_NAME         "ESP32 Speaker"
#define BLE_ADV_INTERVAL_MIN    0x20    /* 20ms */
#define BLE_ADV_INTERVAL_MAX    0x40    /* 40ms */

/*
 * Callback function type for settings changes
 * Called when a BLE write command changes settings
 */
typedef void (*ble_dsp_settings_cb_t)(void);

/*
 * Initialize BLE GATT DSP control service
 *
 * @param settings_changed_cb Callback when settings change (for NVS persistence)
 * @return ESP_OK on success
 */
esp_err_t ble_gatt_dsp_init(ble_dsp_settings_cb_t settings_changed_cb);

/*
 * Start BLE advertising
 * Call after initialization to make device discoverable
 *
 * @return ESP_OK on success
 */
esp_err_t ble_gatt_dsp_start_advertising(void);

/*
 * Stop BLE advertising
 *
 * @return ESP_OK on success
 */
esp_err_t ble_gatt_dsp_stop_advertising(void);

/*
 * Send status notification to connected client
 * Called after settings change or when client requests status
 *
 * @return ESP_OK on success
 */
esp_err_t ble_gatt_dsp_notify_status(void);

/*
 * Check if a BLE client is connected
 *
 * @return true if connected
 */
bool ble_gatt_dsp_is_connected(void);

/*
 * Get BLE connection handle (for internal use)
 *
 * @return Connection handle, or 0xFFFF if not connected
 */
uint16_t ble_gatt_dsp_get_conn_handle(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_GATT_DSP_H */
