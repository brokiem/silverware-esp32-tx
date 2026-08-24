#pragma once
#include <stdint.h>
#include <stdbool.h>

void xn297_init();
void xn297_set_tx_address(const uint8_t* addr);
void xn297_set_rx_address(const uint8_t* addr, uint8_t payload_len);
void xn297_write_payload(const uint8_t* msg, uint8_t len);
bool xn297_read_payload(uint8_t* msg, uint8_t len);
void xn297_set_channel(uint8_t channel);
bool xn297_is_rx_ready();
bool xn297_is_tx_done();
void xn297_set_tx_mode();
void xn297_set_rx_mode();
