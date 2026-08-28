#include "settings.h"
#include <esp_random.h>
#include <nvs.h>

static const char* NVS_NAMESPACE = "bayang_tx";
static const char* NVS_KEY_TXID = "txid";
static const char* NVS_KEY_AUX = "aux";

static bool nvs_read_id(void*, uint8_t* tx_id) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
        return false;

    size_t required_size = 5;
    err = nvs_get_blob(handle, NVS_KEY_TXID, tx_id, &required_size);
    nvs_close(handle);

    return err == ESP_OK && required_size == 5;
}

static bool nvs_write_id(void*, const uint8_t* tx_id) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return false;
    err = nvs_set_blob(handle, NVS_KEY_TXID, tx_id, 5);
    if (err == ESP_OK)
        err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

static bool nvs_read_aux(void*, uint8_t* aux_flags) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
        return false;
    err = nvs_get_u8(handle, NVS_KEY_AUX, aux_flags);
    nvs_close(handle);
    return err == ESP_OK;
}

static bool nvs_write_aux(void*, uint8_t aux_flags) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return false;
    err = nvs_set_u8(handle, NVS_KEY_AUX, aux_flags);
    if (err == ESP_OK)
        err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

static const SettingsStorageAdapter nvs_storage = {nullptr, nvs_read_id, nvs_write_id, nvs_read_aux, nvs_write_aux};

bool load_transmitter_id(uint8_t* tx_id) {
    return load_transmitter_id_from(nvs_storage, tx_id);
}

bool generate_and_save_transmitter_id(uint8_t* tx_id) {
    if (!tx_id)
        return false;
    const uint32_t r1 = esp_random();
    const uint32_t r2 = esp_random();

    tx_id[0] = (r1 >> 24) & 0xFF;
    tx_id[1] = (r1 >> 16) & 0xFF;
    tx_id[2] = (r1 >> 8) & 0xFF;
    tx_id[3] = r1 & 0xFF;
    tx_id[4] = r2 & 0xFF;
    if (!transmitter_id_is_valid(tx_id))
        tx_id[0] = 1;

    return save_transmitter_id_to(nvs_storage, tx_id);
}

bool factory_reset_transmitter_id(uint8_t* tx_id) {
    return generate_and_save_transmitter_id(tx_id);
}

bool load_aux_flags(uint8_t* aux_flags) {
    return load_aux_flags_from(nvs_storage, aux_flags);
}

bool save_aux_flags(uint8_t aux_flags) {
    return save_aux_flags_to(nvs_storage, aux_flags);
}
