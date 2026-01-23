/*
 * ESP32 Bluetooth Speaker with DSP and BLE Control
 * FSD-DSP-001: DSP Presets + Loudness via BLE GATT
 *
 * Bluetooth A2DP sink with:
 * - I2S output to MAX98357A DAC
 * - DSP processing (EQ presets, loudness, limiter)
 * - BLE GATT control interface
 * - Persistent settings storage
 *
 * For ESP-IDF v6.0-beta1 or later
 *
 * Author: Robin Kluit
 * Date: 2026-01-20
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"

#include "driver/i2s_std.h"
#include "driver/gpio.h"

/* Project modules */
#include "dsp_processor.h"
#include "ble_gatt_dsp.h"
#include "nvs_settings.h"

static const char *TAG = "BT_SPEAKER";

/* Device name visible on Bluetooth */
#define BT_DEVICE_NAME "42 Decibels"

/* I2S GPIO Configuration for MAX98357A */
#define I2S_BCK_PIN     GPIO_NUM_26
#define I2S_WS_PIN      GPIO_NUM_25
#define I2S_DATA_PIN    GPIO_NUM_22

/* I2S configuration */
#define I2S_SAMPLE_RATE     44100
#define I2S_BITS_PER_SAMPLE I2S_DATA_BIT_WIDTH_16BIT
#define I2S_CHANNEL_NUM     2

/* Watchdog timeout in seconds */
#define WDT_TIMEOUT_SEC     30

/* I2S channel handle */
static i2s_chan_handle_t i2s_tx_handle = NULL;

/* Connection state tracking */
static bool s_a2dp_connected = false;
static bool s_audio_started = false;

/* Current sample rate for DSP */
static uint32_t s_current_sample_rate = I2S_SAMPLE_RATE;

/* Forward declarations */
static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
static void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param);
static void bt_app_a2d_data_cb(const uint8_t *data, uint32_t len);
static void bt_app_avrc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param);
static void settings_changed_callback(void);

/*
 * Initialize I2S for audio output to MAX98357A
 */
static esp_err_t i2s_init(void)
{
    ESP_LOGI(TAG, "Initializing I2S...");

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;

    esp_err_t ret = i2s_new_channel(&chan_cfg, &i2s_tx_handle, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S channel: %s", esp_err_to_name(ret));
        return ret;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(I2S_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_BITS_PER_SAMPLE, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCK_PIN,
            .ws = I2S_WS_PIN,
            .dout = I2S_DATA_PIN,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(i2s_tx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init I2S std mode: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2s_channel_enable(i2s_tx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S channel: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2S initialized successfully");
    return ESP_OK;
}

/*
 * Reconfigure I2S sample rate based on A2DP stream parameters
 */
static esp_err_t i2s_reconfigure(uint32_t sample_rate)
{
    if (i2s_tx_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (sample_rate == s_current_sample_rate) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Reconfiguring I2S to %lu Hz", sample_rate);

    esp_err_t ret = i2s_channel_disable(i2s_tx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disable I2S: %s", esp_err_to_name(ret));
        return ret;
    }

    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    ret = i2s_channel_reconfig_std_clock(i2s_tx_handle, &clk_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconfigure I2S clock: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2s_channel_enable(i2s_tx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Update DSP sample rate */
    s_current_sample_rate = sample_rate;
    dsp_set_sample_rate(sample_rate);

    return ESP_OK;
}

/*
 * GAP callback for handling Bluetooth connection events and pairing
 */
static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Authentication success: %s", param->auth_cmpl.device_name);
        } else {
            ESP_LOGE(TAG, "Authentication failed, status: %d", param->auth_cmpl.stat);
        }
        break;

    case ESP_BT_GAP_PIN_REQ_EVT:
        ESP_LOGI(TAG, "PIN request, using default '0000'");
        esp_bt_pin_code_t pin_code = {'0', '0', '0', '0'};
        esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
        break;

    case ESP_BT_GAP_CFM_REQ_EVT:
        ESP_LOGI(TAG, "Confirm request for numeric comparison, confirming...");
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;

    case ESP_BT_GAP_KEY_NOTIF_EVT:
        ESP_LOGI(TAG, "Passkey notification: %06lu", param->key_notif.passkey);
        break;

    case ESP_BT_GAP_KEY_REQ_EVT:
        ESP_LOGI(TAG, "Passkey request");
        break;

    case ESP_BT_GAP_MODE_CHG_EVT:
        ESP_LOGD(TAG, "Power mode changed: %d", param->mode_chg.mode);
        break;

    default:
        ESP_LOGD(TAG, "GAP event: %d", event);
        break;
    }
}

/*
 * A2DP callback for handling audio streaming events
 */
static void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT:
        if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
            ESP_LOGI(TAG, "A2DP connected");
            s_a2dp_connected = true;
        } else if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            ESP_LOGI(TAG, "A2DP disconnected");
            s_a2dp_connected = false;
            s_audio_started = false;
        }
        break;

    case ESP_A2D_AUDIO_STATE_EVT:
        if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED) {
            ESP_LOGI(TAG, "Audio stream started");
            s_audio_started = true;
        } else if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_SUSPEND) {
            ESP_LOGI(TAG, "Audio stream suspended");
            s_audio_started = false;
        }
        break;

    case ESP_A2D_AUDIO_CFG_EVT:
        ESP_LOGI(TAG, "Audio configuration received");
        if (param->audio_cfg.mcc.type == ESP_A2D_MCT_SBC) {
            uint32_t sample_rate = 44100;
            uint8_t samp_freq = param->audio_cfg.mcc.cie.sbc_info.samp_freq;

            if (samp_freq & 0x01) {
                sample_rate = 48000;
            } else if (samp_freq & 0x02) {
                sample_rate = 44100;
            } else if (samp_freq & 0x04) {
                sample_rate = 32000;
            } else if (samp_freq & 0x08) {
                sample_rate = 16000;
            }

            ESP_LOGI(TAG, "SBC codec, sample rate: %lu Hz", sample_rate);
            i2s_reconfigure(sample_rate);
        }
        break;

    case ESP_A2D_PROF_STATE_EVT:
        if (param->a2d_prof_stat.init_state == ESP_A2D_INIT_SUCCESS) {
            ESP_LOGI(TAG, "A2DP profile initialized");
        }
        break;

    default:
        ESP_LOGD(TAG, "A2DP event: %d", event);
        break;
    }
}

/*
 * A2DP data callback - receives decoded audio, processes through DSP, sends to I2S
 * This is the real-time audio path (FR-13, FR-16, FR-17)
 */
static void bt_app_a2d_data_cb(const uint8_t *data, uint32_t len)
{
    if (i2s_tx_handle == NULL || data == NULL || len == 0) {
        return;
    }

    /* Process audio through DSP chain
     * Note: We cast away const because we modify in place for efficiency (FR-17)
     * The A2DP stack doesn't use the buffer after this callback returns
     */
    dsp_process((int16_t *)data, len / sizeof(int16_t));

    /* Write processed audio to I2S */
    size_t bytes_written = 0;
    esp_err_t ret = i2s_channel_write(i2s_tx_handle, data, len, &bytes_written, portMAX_DELAY);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "I2S write error: %s", esp_err_to_name(ret));
    }
}

/*
 * AVRCP Controller callback for media control events
 */
static void bt_app_avrc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param)
{
    switch (event) {
    case ESP_AVRC_CT_CONNECTION_STATE_EVT:
        ESP_LOGI(TAG, "AVRC %s (A2DP: %s)",
                 param->conn_stat.connected ? "connected" : "disconnected",
                 s_a2dp_connected ? "connected" : "disconnected");
        break;

    case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:
        ESP_LOGD(TAG, "AVRC passthrough response");
        break;

    case ESP_AVRC_CT_METADATA_RSP_EVT:
        ESP_LOGD(TAG, "AVRC metadata response");
        break;

    case ESP_AVRC_CT_PLAY_STATUS_RSP_EVT:
        ESP_LOGD(TAG, "AVRC play status response");
        break;

    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:
        ESP_LOGD(TAG, "AVRC change notification");
        break;

    case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
        ESP_LOGD(TAG, "AVRC remote features: 0x%lx", param->rmt_feats.feat_mask);
        break;

    default:
        ESP_LOGD(TAG, "AVRC event: %d", event);
        break;
    }
}

/*
 * Callback when DSP settings change via BLE (for NVS persistence)
 */
static void settings_changed_callback(void)
{
    /* Update NVS with current DSP settings */
    nvs_settings_update((uint8_t)dsp_get_preset(), dsp_get_loudness() ? 1 : 0);
}

/*
 * Initialize Bluetooth stack (dual mode: Classic + BLE)
 */
static esp_err_t bluetooth_init(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Initializing Bluetooth (dual mode)...");

    /* Initialize BT controller in dual mode */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT controller init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Enable dual mode (Classic BT + BLE) */
    ret = esp_bt_controller_enable(ESP_BT_MODE_BTDM);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT controller enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Initialize Bluedroid */
    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    ret = esp_bluedroid_init_with_cfg(&bluedroid_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Set device name */
    esp_bt_gap_set_device_name(BT_DEVICE_NAME);

    /* Register GAP callback for pairing (Classic BT) */
    esp_bt_gap_register_callback(bt_app_gap_cb);

    /* Configure Secure Simple Pairing (SSP) for iOS compatibility (FR-2) */
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_IO;
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));

    /* Enable SSP */
    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_VARIABLE;
    esp_bt_pin_code_t pin_code;
    esp_bt_gap_set_pin(pin_type, 0, pin_code);

    /* Initialize A2DP sink */
    ret = esp_a2d_register_callback(bt_app_a2d_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "A2DP register callback failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_a2d_sink_register_data_callback(bt_app_a2d_data_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "A2DP register data callback failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_a2d_sink_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "A2DP sink init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Initialize AVRCP controller */
    ret = esp_avrc_ct_register_callback(bt_app_avrc_ct_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AVRC register callback failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_avrc_ct_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AVRC controller init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Set discoverable and connectable mode for Classic BT */
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    ESP_LOGI(TAG, "Classic Bluetooth initialized, device name: %s", BT_DEVICE_NAME);

    /* Initialize BLE GATT DSP control service */
    ret = ble_gatt_dsp_init(settings_changed_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BLE GATT DSP init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "BLE GATT service initialized");

    return ESP_OK;
}

/*
 * Watchdog monitoring task - keeps system alive and monitors health
 */
static void watchdog_task(void *arg)
{
    ESP_LOGI(TAG, "Watchdog task started");

    /* Subscribe this task to the watchdog */
    esp_task_wdt_add(NULL);

    while (1) {
        /* Feed watchdog periodically to prevent system reset */
        esp_task_wdt_reset();
        if (s_audio_started) {
            /* Placeholder for streaming health monitoring */
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/*
 * Application entry point
 */
void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32 Bluetooth Speaker with DSP ===");
    ESP_LOGI(TAG, "FSD-DSP-001: DSP Presets + Loudness via BLE GATT");
    ESP_LOGI(TAG, "Firmware version: 2.0");

    /* Initialize NVS (required for Bluetooth and settings storage) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS flash erase required");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Initialize NVS settings module */
    ret = nvs_settings_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS settings init failed, using defaults");
    }

    /* Configure Task Watchdog Timer for crash recovery (FR-6) */
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WDT_TIMEOUT_SEC * 1000,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    ESP_ERROR_CHECK(esp_task_wdt_reconfigure(&wdt_config));

    /* Initialize DSP processor */
    ret = dsp_init(I2S_SAMPLE_RATE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DSP initialization failed, restarting...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }

    /* Load stored settings and apply to DSP */
    nvs_dsp_settings_t settings;
    nvs_settings_get(&settings);
    dsp_set_preset((dsp_preset_t)settings.preset_id);
    dsp_set_loudness(settings.loudness != 0);
    ESP_LOGI(TAG, "Loaded settings: preset=%s, loudness=%s",
             dsp_preset_name((dsp_preset_t)settings.preset_id),
             settings.loudness ? "ON" : "OFF");

    /* Initialize I2S for audio output */
    ret = i2s_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S initialization failed, restarting...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }

    /* Initialize Bluetooth (Classic + BLE) */
    ret = bluetooth_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth initialization failed, restarting...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }

    /* Create watchdog monitoring task */
    xTaskCreate(watchdog_task, "watchdog", 2048, NULL, 5, NULL);

    ESP_LOGI(TAG, "System ready");
    ESP_LOGI(TAG, "- Classic BT: Waiting for A2DP audio connection");
    ESP_LOGI(TAG, "- BLE: DSP control service advertising");
    ESP_LOGI(TAG, "- DSP: %s preset, loudness %s",
             dsp_preset_name(dsp_get_preset()),
             dsp_get_loudness() ? "ON" : "OFF");
}
