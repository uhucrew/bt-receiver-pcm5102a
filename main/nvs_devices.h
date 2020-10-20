#pragma once

#define STORAGE_NAMESPACE "bt_device_names"



esp_err_t update_remote_name(esp_bd_addr_t id, uint8_t *name);
esp_err_t get_remote_name(esp_bd_addr_t id, uint8_t **name);
