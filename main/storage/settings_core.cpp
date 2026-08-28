#include "settings.h"

bool transmitter_id_is_valid(const uint8_t* tx_id) {
    if (tx_id == nullptr)
        return false;
    uint8_t combined = 0;
    for (uint8_t i = 0; i < 5; ++i)
        combined |= tx_id[i];
    return combined != 0;
}

bool load_transmitter_id_from(const SettingsStorageAdapter& storage, uint8_t* tx_id) {
    return tx_id != nullptr && storage.readId != nullptr && storage.readId(storage.context, tx_id) &&
           transmitter_id_is_valid(tx_id);
}

bool save_transmitter_id_to(const SettingsStorageAdapter& storage, const uint8_t* tx_id) {
    return transmitter_id_is_valid(tx_id) && storage.writeId != nullptr && storage.writeId(storage.context, tx_id);
}

bool load_aux_flags_from(const SettingsStorageAdapter& storage, uint8_t* aux_flags) {
    if (aux_flags == nullptr || storage.readAux == nullptr || !storage.readAux(storage.context, aux_flags))
        return false;
    return (*aux_flags & ~SETTINGS_AUX_MASK) == 0;
}

bool save_aux_flags_to(const SettingsStorageAdapter& storage, uint8_t aux_flags) {
    return (aux_flags & ~SETTINGS_AUX_MASK) == 0 && storage.writeAux != nullptr &&
           storage.writeAux(storage.context, aux_flags);
}
