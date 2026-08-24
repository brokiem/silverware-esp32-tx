#pragma once
#include <stdint.h>
#include <stdbool.h>

// NRF24 Commands
#define NRF24_R_REGISTER    0x00
#define NRF24_W_REGISTER    0x20
#define NRF24_R_RX_PAYLOAD  0x61
#define NRF24_W_TX_PAYLOAD  0xA0
#define NRF24_FLUSH_TX      0xE1
#define NRF24_FLUSH_RX      0xE2
#define NRF24_REUSE_TX_PL   0xE3
#define NRF24_NOP           0xFF

// NRF24 Registers
#define NRF24_REG_CONFIG      0x00
#define NRF24_REG_EN_AA       0x01
#define NRF24_REG_EN_RXADDR   0x02
#define NRF24_REG_SETUP_AW    0x03
#define NRF24_REG_SETUP_RETR  0x04
#define NRF24_REG_RF_CH       0x05
#define NRF24_REG_RF_SETUP    0x06
#define NRF24_REG_STATUS      0x07
#define NRF24_REG_OBSERVE_TX  0x08
#define NRF24_REG_RX_ADDR_P0  0x0A
#define NRF24_REG_TX_ADDR     0x10
#define NRF24_REG_RX_PW_P0    0x11
#define NRF24_REG_FIFO_STATUS 0x17
#define NRF24_REG_DYNPD       0x1C
#define NRF24_REG_FEATURE     0x1D

// Flags
#define NRF24_RX_DR           0x40
#define NRF24_TX_DS           0x20
#define NRF24_MAX_RT          0x10

void nrf24_init();
bool nrf24_test();
uint8_t nrf24_read_reg(uint8_t reg);
void nrf24_write_reg(uint8_t reg, uint8_t value);
void nrf24_read_reg_multi(uint8_t reg, uint8_t* buf, uint8_t len);
void nrf24_write_reg_multi(uint8_t reg, const uint8_t* buf, uint8_t len);
void nrf24_flush_tx();
void nrf24_flush_rx();
void nrf24_write_payload(const uint8_t* buf, uint8_t len);
void nrf24_read_payload(uint8_t* buf, uint8_t len);
void nrf24_set_tx_mode();
void nrf24_set_rx_mode();
void nrf24_set_standby();
void nrf24_set_channel(uint8_t channel);
void nrf24_clear_irq_flags();
uint8_t nrf24_get_status();
