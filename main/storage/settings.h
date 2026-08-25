#pragma once
#include <stdbool.h>
#include <stdint.h>

struct SettingsStorageAdapter {
    void* context;
    bool (*readId)(void* context, uint8_t* tx_id);
    bool (*writeId)(void* context, const uint8_t* tx_id);
};

bool transmitter_id_is_valid(const uint8_t* tx_id);
bool load_transmitter_id_from(const SettingsStorageAdapter& storage, uint8_t* tx_id);
bool save_transmitter_id_to(const SettingsStorageAdapter& storage, const uint8_t* tx_id);

bool load_transmitter_id(uint8_t* tx_id);
bool generate_and_save_transmitter_id(uint8_t* tx_id);
bool factory_reset_transmitter_id(uint8_t* tx_id);
