// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_log.h"

#include "esp_bt.h"
#include "bt_app_core.h"
#include "bt_app_av.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "driver/i2s.h"

#include "nvs_devices.h"
#include "i2c_x.h"
#include "display.h"
#include "button.h"

/* event for handler "bt_av_hdl_stack_up */
enum {
    BT_APP_EVT_STACK_UP = 0,
};

/* handler for bluetooth stack enabled events */
static void bt_av_hdl_stack_evt(uint16_t event, void *p_param);

const char *dev_name = "UHU";
const int32_t default_sample_rate = 44100;
static const int32_t volume_default = (int32_t)round(55.0 * 0x7f / 100.0);
uint8_t *remote_name = NULL;


static void process_button_task()
{
    button_event_t ev;
    bool bt_minus = false;
    bool bt_minus_long = false;
    bool bt_plus = false;
    bool bt_plus_long = false;
    bool vol_muted = false;
    bool reboot = false;
    QueueHandle_t button_events = button_init(PIN_BIT(CONFIG_BUTTON_PLUS_PIN) | PIN_BIT(CONFIG_BUTTON_MINUS_PIN));
    for ( ;; ) {
        if (xQueueReceive(button_events, &ev, 1000 / portTICK_PERIOD_MS)) {
            if (reboot) {
                if (ev.event == BUTTON_UP) {
                    fflush(stdout);
                    esp_restart();
                }
                continue;
            }
            if (ev.pin == CONFIG_BUTTON_MINUS_PIN) {
                if (ev.event == BUTTON_DOWN) {
                    bt_minus = true;
                    if (bt_minus_long) {
                        volume_down(5);
                        vTaskDelay(15 / portTICK_PERIOD_MS);
                    }
                    else {
                        volume_down(1);
                    }
                }
                else if (ev.event == BUTTON_LONG) {
                    bt_minus_long = true;
                }
                else if (ev.event == BUTTON_UP) {
                    bt_minus = false;
                    bt_minus_long = false;
                }
            }
            if (ev.pin == CONFIG_BUTTON_PLUS_PIN) {
                if (ev.event == BUTTON_DOWN) {
                    bt_plus = true;
                    if (bt_plus_long) {
                        volume_up(5);
                        vTaskDelay(15 / portTICK_PERIOD_MS);
                    }
                    else {
                        volume_up(1);
                    }
                }
                else if (ev.event == BUTTON_LONG) {
                    bt_plus_long = true;
                }
                else if (ev.event == BUTTON_UP) {
                    bt_plus = false;
                    bt_plus_long = false;
                }
            }
        }
        //both buttons pressed -> mute or restore
        if (bt_minus && bt_plus && !bt_minus_long && !bt_plus_long) {
            if (vol_muted) {
                vol_muted = false;
                volume_restore();
            }
            else {
                vol_muted = true;
                volume_mute();
            }
        }
        //both buttons pressed long -> reboot
        if ((bt_minus_long && bt_plus) || (bt_plus_long && bt_minus)) {
            reboot = true;
            display_reboot();
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}



void app_main(void)
{
    /* Initialize NVS — it is used to store PHY calibration data */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    i2c_init();
    display_init();

    i2s_config_t i2s_config = {
#ifdef CONFIG_A2DP_SINK_OUTPUT_INTERNAL_DAC
        .mode = I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN,
#else
        .mode = I2S_MODE_MASTER | I2S_MODE_TX,                                  // Only TX
#endif
        .sample_rate = default_sample_rate,
        .bits_per_sample = 32,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,                           //2-channels
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .dma_buf_count = 12,
        .dma_buf_len = 120,
        .intr_alloc_flags = 0,                                                  //Default interrupt priority
        .tx_desc_auto_clear = true                                              //Auto clear tx descriptor on underflow
    };


    i2s_driver_install(0, &i2s_config, 0, NULL);
#ifdef CONFIG_A2DP_SINK_OUTPUT_INTERNAL_DAC
    i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
    i2s_set_pin(0, NULL);
#else
    i2s_pin_config_t pin_config = {
        .bck_io_num = CONFIG_I2S_BCK_PIN,
        .ws_io_num = CONFIG_I2S_LRCK_PIN,
        .data_out_num = CONFIG_I2S_DATA_PIN,
        .data_in_num = -1                                                       //Not used
    };

    i2s_set_pin(0, &pin_config);
#endif

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if ((err = esp_bt_controller_init(&bt_cfg)) != ESP_OK) {
        ESP_LOGE(BT_AV_TAG, "%s initialize controller failed: %s", __func__, esp_err_to_name(err));
        return;
    }

    if ((err = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)) != ESP_OK) {
        ESP_LOGE(BT_AV_TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(err));
        return;
    }

    if ((err = esp_bluedroid_init()) != ESP_OK) {
        ESP_LOGE(BT_AV_TAG, "%s initialize bluedroid failed: %s", __func__, esp_err_to_name(err));
        return;
    }

    if ((err = esp_bluedroid_enable()) != ESP_OK) {
        ESP_LOGE(BT_AV_TAG, "%s enable bluedroid failed: %s", __func__, esp_err_to_name(err));
        return;
    }

    /* create application task */
    bt_app_task_start_up();

    /* Bluetooth device name, connection mode and profile set up */
    bt_app_work_dispatch(bt_av_hdl_stack_evt, BT_APP_EVT_STACK_UP, NULL, 0, NULL);

#if (CONFIG_BT_SSP_ENABLED == true)
    /* Set default parameters for Secure Simple Pairing */
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_IO;
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));
#endif

    /*
     * Set default parameters for Legacy Pairing
     * Use fixed pin code
     */
    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
    esp_bt_pin_code_t pin_code;
    pin_code[0] = '0';
    pin_code[1] = '0';
    pin_code[2] = '0';
    pin_code[3] = '0';
    esp_bt_gap_set_pin(pin_type, 4, pin_code);

    xTaskCreate(
        process_button_task,    /* Task function. */
        "ProcessButton",        /* String with name of task. */
        10000,                  /* Stack size in bytes. */
        NULL,                   /* Parameter passed as input of the task */
        0,                      /* Priority of the task. */
        NULL
    );                          /* Task handle. */

    ESP_LOGI(BT_AV_TAG, "tasks created: app_main finished: core: %u", xPortGetCoreID());
}

void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    static char str_buf[16];

    switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT: {
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(BT_AV_TAG, "authentication success: %s", param->auth_cmpl.device_name);
            update_remote_name(param->auth_cmpl.bda, param->auth_cmpl.device_name);
            get_remote_name(param->auth_cmpl.bda, &remote_name);
            esp_log_buffer_hex(BT_AV_TAG, param->auth_cmpl.bda, ESP_BD_ADDR_LEN);
        } else {
            ESP_LOGE(BT_AV_TAG, "authentication failed, status:%d", param->auth_cmpl.stat);
        }
        break;
    }

#if (CONFIG_BT_SSP_ENABLED == true)
    case ESP_BT_GAP_CFM_REQ_EVT:
        ESP_LOGI(BT_AV_TAG, "ESP_BT_GAP_CFM_REQ_EVT Please compare the numeric value: %d", param->cfm_req.num_val);

        snprintf(str_buf, sizeof(str_buf), "verify %d", param->cfm_req.num_val);
        display_state(str_buf, NULL, 0);

        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    case ESP_BT_GAP_KEY_NOTIF_EVT:
        ESP_LOGI(BT_AV_TAG, "ESP_BT_GAP_KEY_NOTIF_EVT passkey: %d", param->key_notif.passkey);

        snprintf(str_buf, sizeof(str_buf), "passwd %d", param->cfm_req.num_val);
        display_state(str_buf, NULL, 0);

        break;
    case ESP_BT_GAP_KEY_REQ_EVT:
        ESP_LOGI(BT_AV_TAG, "ESP_BT_GAP_KEY_REQ_EVT Please enter passkey!");

        display_state("enter passwd", NULL, 0);

        break;
#endif

    default: {
        ESP_LOGI(BT_AV_TAG, "event: %d", event);
        break;
    }
    }
    return;
}
static void bt_av_hdl_stack_evt(uint16_t event, void *p_param)
{
    ESP_LOGD(BT_AV_TAG, "%s evt %d", __func__, event);
    switch (event) {
    case BT_APP_EVT_STACK_UP: {
        /* set up device name */
        esp_bt_dev_set_device_name(dev_name);

        esp_bt_gap_register_callback(bt_app_gap_cb);

        /* initialize AVRCP controller */
        esp_avrc_ct_init();
        esp_avrc_ct_register_callback(bt_app_rc_ct_cb);
        /* initialize AVRCP target */
        assert (esp_avrc_tg_init() == ESP_OK);
        esp_avrc_tg_register_callback(bt_app_rc_tg_cb);

        esp_avrc_rn_evt_cap_mask_t evt_set = {0};
        esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &evt_set, ESP_AVRC_RN_VOLUME_CHANGE);
        assert(esp_avrc_tg_set_rn_evt_cap(&evt_set) == ESP_OK);

        /* initialize A2DP sink */
        esp_a2d_register_callback(&bt_app_a2d_cb);
        esp_a2d_sink_register_data_callback(bt_app_a2d_data_cb);
        esp_a2d_sink_init();

        /* set discoverable and connectable mode, wait to be connected */
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

        volume_set_by_local_host(volume_default);

        display_state("ready to pair", NULL, 0);

        break;
    }
    default:
        ESP_LOGE(BT_AV_TAG, "%s unhandled evt %d", __func__, event);
        break;
    }
}
