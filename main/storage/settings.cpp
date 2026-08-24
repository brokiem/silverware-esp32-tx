#include "settings.h"
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_system.h>
#include <esp_random.h>
#include <string.h>
#include <stdio.h>

static const char* NVS_NAMESPACE = "bayang_tx";
static const char* NVS_KEY_TXID = "txid";

bool load_transmitter_id(uint8_t* tx_id) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return false;

    size_t required_size = 5;
    err = nvs_get_blob(handle, NVS_KEY_TXID, tx_id, &required_size);
    nvs_close(handle);

    return (err == ESP_OK && required_size == 5);
}

void generate_and_save_transmitter_id(uint8_t* tx_id) {
    // Generate a new random ID
    uint32_t r1 = esp_random();
    uint32_t r2 = esp_random();
    
    tx_id[0] = (r1 >> 24) & 0xFF;
    tx_id[1] = (r1 >> 16) & 0xFF;
    tx_id[2] = (r1 >> 8) & 0xFF;
    tx_id[3] = r1 & 0xFF;
    tx_id[4] = r2 & 0xFF;

    // Avoid 0,0,0,0,0 which is bind address
    if (tx_id[0] == 0 && tx_id[1] == 0 && tx_id[2] == 0 && tx_id[3] == 0 && tx_id[4] == 0) {
        tx_id[0] = 1;
    }

    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_blob(handle, NVS_KEY_TXID, tx_id, 5);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

void factory_reset_transmitter_id(uint8_t* tx_id) {
    generate_and_save_transmitter_id(tx_id);
}
