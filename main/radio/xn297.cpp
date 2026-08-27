#include "xn297.h"
#include <Arduino.h>
#include <string.h>
#include "nrf24.h"
#include "radio_validation.h"

static uint8_t xn297_tx_addr[5];

const uint8_t xn297_scramble[] = {0xE3, 0xB1, 0x4B, 0xEA, 0x85, 0xBC, 0xE5, 0x66, 0x0D, 0xAE, 0x8C, 0x88, 0x12,
                                  0x69, 0xEE, 0x1F, 0xC7, 0x62, 0x97, 0xD5, 0x0B, 0x79, 0xCA, 0xCC, 0x1B, 0x5D,
                                  0x19, 0x10, 0x24, 0xD3, 0xDC, 0x3F, 0x8E, 0xC5, 0x2F, 0xAA, 0x16, 0xF3, 0x95};

const uint16_t xn297_crc_xorout_scrambled[] = {0x0000, 0x3448, 0x9BA7, 0x8BBB, 0x85E1, 0x3E8C, 0x451E, 0x18E6, 0x6B24,
                                               0xE7AB, 0x3828, 0x814B, 0xD461, 0xF494, 0x2503, 0x691D, 0xFE8B, 0x9BA7,
                                               0x8B17, 0x2920, 0x8B5F, 0x61B1, 0xD391, 0x7401, 0x2138, 0x129F, 0xB3A0,
                                               0x2988, 0x23CA, 0xC0CB, 0x0C6C, 0xB329, 0xA0A1, 0x0A16, 0xA9D0};

static uint8_t bit_reverse(uint8_t b) {
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
    return b;
}

static void crc16_update(uint16_t* crc, uint8_t data) {
    for (uint8_t i = 0; i < 8; i++) {
        if (((*crc >> 8) ^ data) & 0x80)
            *crc = (*crc << 1) ^ 0x1021;
        else
            *crc <<= 1;
        data <<= 1;
    }
}

bool xn297_init() {
    return nrf24_init();
}

void xn297_set_tx_address(const uint8_t* addr) {
    memcpy(xn297_tx_addr, addr, 5);

    // NRF24 physical address matches XN297 preamble/address structure
    uint8_t physical_tx_addr[] = {0x55, 0x0F, 0x71, 0x0C, 0x00};
    nrf24_write_reg_multi(NRF24_REG_TX_ADDR, physical_tx_addr, 5);
}

bool xn297_set_rx_address(const uint8_t* addr, uint8_t payload_len) {
    if (!addr || !nrf24_payload_length_valid(payload_len))
        return false;
    uint8_t physical_rx_addr[5];
    for (uint8_t i = 0; i < 5; ++i) {
        physical_rx_addr[i] = addr[i] ^ xn297_scramble[4 - i];
    }
    nrf24_write_reg_multi(NRF24_REG_RX_ADDR_P0, physical_rx_addr, 5);
    nrf24_write_reg(NRF24_REG_RX_PW_P0, payload_len);
    return true;
}

bool xn297_write_payload(const uint8_t* msg, uint8_t len) {
    if (!msg || !xn297_tx_payload_length_valid(len))
        return false;
    uint8_t buf[32];
    uint8_t last = 0;

    // Encode address
    for (uint8_t i = 0; i < 5; ++i) {
        buf[last] = xn297_tx_addr[4 - i] ^ xn297_scramble[i];
        last++;
    }

    // Encode payload
    for (uint8_t i = 0; i < len; ++i) {
        buf[last] = bit_reverse(msg[i]) ^ xn297_scramble[5 + i];
        last++;
    }

    // Compute CRC over the address and payload
    uint16_t crc = 0xb5d2;
    for (uint8_t i = 0; i < last; ++i) {
        crc16_update(&crc, buf[i]);
    }

    crc ^= xn297_crc_xorout_scrambled[5 - 3 + len];
    buf[last++] = crc >> 8;
    buf[last++] = crc & 0xFF;

    return nrf24_write_payload(buf, last);
}

bool xn297_read_payload(uint8_t* msg, uint8_t len) {
    if (!msg || !nrf24_payload_length_valid(len))
        return false;
    uint8_t buf[32];
    if (!nrf24_read_payload(buf, len))
        return false;

    // Decode payload
    for (uint8_t i = 0; i < len; i++) {
        uint8_t b_in = buf[i] ^ xn297_scramble[5 + i];
        msg[i] = bit_reverse(b_in);
    }

    // Original NFE Silverware telemetry is sent without an XN297 CRC.
    return true;
}

void xn297_set_channel(uint8_t channel) {
    nrf24_set_channel(channel);
}

bool xn297_is_rx_ready() {
    return (nrf24_get_status() & NRF24_RX_DR) != 0;
}

bool xn297_irq_asserted() {
    return nrf24_irq_asserted();
}

Xn297TxStatus xn297_get_tx_status() {
    return xn297_decode_tx_status(nrf24_get_status());
}

void xn297_set_tx_mode() {
    nrf24_clear_irq_flags();
    nrf24_set_tx_mode();
}

void xn297_set_rx_mode() {
    nrf24_clear_irq_flags();
    nrf24_set_rx_mode();
}
