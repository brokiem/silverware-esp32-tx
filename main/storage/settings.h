#pragma once
#include <stdbool.h>
#include <stdint.h>

struct SettingsStorageAdapter {
    void* context;
    bool (*readId)(void* context, uint8_t* tx_id);
    bool (*writeId)(void* context, const uint8_t* tx_id);
    bool (*readAux)(void* context, uint8_t* aux_flags);
    bool (*writeAux)(void* context, uint8_t aux_flags);
};

constexpr uint8_t SETTINGS_AUX_MASK = 0x1F;

bool transmitter_id_is_valid(const uint8_t* tx_id);
bool load_transmitter_id_from(const SettingsStorageAdapter& storage, uint8_t* tx_id);
bool save_transmitter_id_to(const SettingsStorageAdapter& storage, const uint8_t* tx_id);
bool load_aux_flags_from(const SettingsStorageAdapter& storage, uint8_t* aux_flags);
bool save_aux_flags_to(const SettingsStorageAdapter& storage, uint8_t aux_flags);

bool load_transmitter_id(uint8_t* tx_id);
bool generate_and_save_transmitter_id(uint8_t* tx_id);
bool factory_reset_transmitter_id(uint8_t* tx_id);
bool load_aux_flags(uint8_t* aux_flags);
bool save_aux_flags(uint8_t aux_flags);
