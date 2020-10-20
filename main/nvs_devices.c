#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include "nvs_flash.h"
#include "nvs.h"

#include "esp_bt_device.h"


#include "nvs_devices.h"


#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
static const char *TAG = "NVS";
#pragma GCC diagnostic pop

static const char unknown_device[] = "unknown device";


static void bt_addr_to_string(esp_bd_addr_t bda, char *str) {
    snprintf(str, 13, "%02x%02x%02x%02x%02x%02x", bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
}



esp_err_t get_remote_name(esp_bd_addr_t bda, uint8_t **name) {
    nvs_handle_t load_handle;
    esp_err_t err;

    static char key[13];
    key[12] = 0;
    bt_addr_to_string(bda, (char *)&key);

    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &load_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: nvs_open failed: %d", __func__, err);
        nvs_close(load_handle);
        return err;
    }

    size_t required_size;
    err = nvs_get_str(load_handle, key, NULL, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        if (*name != NULL) free(*name);
        *name = malloc(strlen(unknown_device) + 1);
        strcpy((char *)*name, unknown_device);
        nvs_close(load_handle);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: nvs_get_str failed: %d", __func__, err);
        nvs_close(load_handle);
        return err;
    }

    if (*name != NULL) free(*name);
    *name = malloc(required_size);
    if (*name == NULL) {
        ESP_LOGE(TAG, "%s: malloc(%d) failed", __func__, required_size);
        nvs_close(load_handle);
        return err;
    }
    nvs_get_str(load_handle, key, (char *)*name, &required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: nvs_get_str failed: %d", __func__, err);
    }

    nvs_close(load_handle);
    return err;
}

esp_err_t update_remote_name(esp_bd_addr_t bda, uint8_t *name) {
    nvs_handle_t save_handle;
    esp_err_t err;

    static char key[13];
    key[12] = 0;
    bt_addr_to_string(bda, (char *)&key);

    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &save_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: nvs_open failed: %d", __func__, err);
        nvs_close(save_handle);
        return err;
    }

    err = nvs_set_str(save_handle, key, (char *)name);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: nvs_set_str failed: %d", __func__, err);
        nvs_close(save_handle);
        return err;
    }

    err = nvs_commit(save_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: nvs_commit failed: %d", __func__, err);
    }

    nvs_close(save_handle);
    return err;
}
