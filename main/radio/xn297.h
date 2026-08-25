#pragma once
#include <stdbool.h>
#include <stdint.h>

enum class Xn297TxStatus : uint8_t {
    Pending,
    Sent,
    Error,
};

bool xn297_init();
void xn297_set_tx_address(const uint8_t* addr);
bool xn297_set_rx_address(const uint8_t* addr, uint8_t payload_len);
bool xn297_write_payload(const uint8_t* msg, uint8_t len);
bool xn297_read_payload(uint8_t* msg, uint8_t len);
void xn297_set_channel(uint8_t channel);
bool xn297_is_rx_ready();
Xn297TxStatus xn297_get_tx_status();
void xn297_set_tx_mode();
void xn297_set_rx_mode();
