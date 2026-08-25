#pragma once

#include <stdint.h>

#include "xn297.h"

inline constexpr bool nrf24_payload_length_valid(uint8_t length) {
    return length >= 1 && length <= 32;
}

inline constexpr bool xn297_tx_payload_length_valid(uint8_t length) {
    return length >= 1 && length <= 25;
}

inline constexpr Xn297TxStatus xn297_decode_tx_status(uint8_t status) {
    constexpr uint8_t tx_sent = 0x20;
    constexpr uint8_t max_retries = 0x10;
    if ((status & max_retries) != 0)
        return Xn297TxStatus::Error;
    if ((status & tx_sent) != 0)
        return Xn297TxStatus::Sent;
    return Xn297TxStatus::Pending;
}
