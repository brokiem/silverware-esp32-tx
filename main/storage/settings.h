#pragma once
#include <stdint.h>
#include <stdbool.h>

bool load_transmitter_id(uint8_t* tx_id);
void generate_and_save_transmitter_id(uint8_t* tx_id);
void factory_reset_transmitter_id(uint8_t* tx_id);
