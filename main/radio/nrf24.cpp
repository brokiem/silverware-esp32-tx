#include "nrf24.h"
#include <Arduino.h>
#include <SPI.h>
#include "../config.h"
#include "radio_validation.h"

static SPIClass* spi = nullptr;

bool nrf24_init() {
    if (!spi) {
        spi = new SPIClass(VSPI);
        spi->begin(PIN_NRF_SCK, PIN_NRF_MISO, PIN_NRF_MOSI, PIN_NRF_CSN);
    }
    pinMode(PIN_NRF_CE, OUTPUT);
    digitalWrite(PIN_NRF_CE, LOW);
    pinMode(PIN_NRF_CSN, OUTPUT);
    digitalWrite(PIN_NRF_CSN, HIGH);
    pinMode(PIN_NRF_IRQ, INPUT_PULLUP);

    // Default config matching Multiprotocol XN297_EMU setup for NRF24
    nrf24_set_standby();
    nrf24_flush_tx();
    nrf24_flush_rx();
    nrf24_write_reg(NRF24_REG_EN_AA, 0x00);       // No auto acknowledgement
    nrf24_write_reg(NRF24_REG_EN_RXADDR, 0x01);   // Enable data pipe 0 only
    nrf24_write_reg(NRF24_REG_SETUP_AW, 0x03);    // 5 bytes rx/tx address
    nrf24_write_reg(NRF24_REG_SETUP_RETR, 0x00);  // no retransmits

    // Set RF Power
    uint8_t rf_setup = 0x00;
#if BAYANG_RF_POWER == 0
    rf_setup = 0x00;  // -18dBm
#elif BAYANG_RF_POWER == 1
    rf_setup = 0x02;  // -12dBm
#elif BAYANG_RF_POWER == 2
    rf_setup = 0x04;  // -6dBm
#else
    rf_setup = 0x06;  // 0dBm
#endif
    nrf24_write_reg(NRF24_REG_RF_SETUP, rf_setup);  // 1Mbps
    nrf24_write_reg(NRF24_REG_DYNPD, 0x00);         // Disable dynamic payload length
    nrf24_write_reg(NRF24_REG_FEATURE, 0x01);       // Allow NO_ACK

    if (!nrf24_test())
        return false;

    // Power up into TX standby once. Subsequent TX/RX changes keep PWR_UP set.
    nrf24_write_reg(NRF24_REG_CONFIG, 0x02);
    delayMicroseconds(2000);
    return true;
}

bool nrf24_test() {
    uint8_t original = nrf24_read_reg(NRF24_REG_RF_CH);
    nrf24_write_reg(NRF24_REG_RF_CH, 0x55);
    uint8_t test = nrf24_read_reg(NRF24_REG_RF_CH);
    nrf24_write_reg(NRF24_REG_RF_CH, original);
    return (test == 0x55);
}

uint8_t nrf24_read_reg(uint8_t reg) {
    uint8_t val;
    nrf24_read_reg_multi(reg, &val, 1);
    return val;
}

void nrf24_write_reg(uint8_t reg, uint8_t value) {
    nrf24_write_reg_multi(reg, &value, 1);
}

void nrf24_read_reg_multi(uint8_t reg, uint8_t* buf, uint8_t len) {
    digitalWrite(PIN_NRF_CSN, LOW);
    spi->beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
    spi->transfer(NRF24_R_REGISTER | (reg & 0x1F));
    for (uint8_t i = 0; i < len; i++) {
        buf[i] = spi->transfer(0xFF);
    }
    spi->endTransaction();
    digitalWrite(PIN_NRF_CSN, HIGH);
}

void nrf24_write_reg_multi(uint8_t reg, const uint8_t* buf, uint8_t len) {
    digitalWrite(PIN_NRF_CSN, LOW);
    spi->beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
    spi->transfer(NRF24_W_REGISTER | (reg & 0x1F));
    for (uint8_t i = 0; i < len; i++) {
        spi->transfer(buf[i]);
    }
    spi->endTransaction();
    digitalWrite(PIN_NRF_CSN, HIGH);
}

void nrf24_flush_tx() {
    digitalWrite(PIN_NRF_CSN, LOW);
    spi->beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
    spi->transfer(NRF24_FLUSH_TX);
    spi->endTransaction();
    digitalWrite(PIN_NRF_CSN, HIGH);
}

void nrf24_flush_rx() {
    digitalWrite(PIN_NRF_CSN, LOW);
    spi->beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
    spi->transfer(NRF24_FLUSH_RX);
    spi->endTransaction();
    digitalWrite(PIN_NRF_CSN, HIGH);
}

bool nrf24_write_payload(const uint8_t* buf, uint8_t len) {
    if (!buf || !nrf24_payload_length_valid(len))
        return false;
    digitalWrite(PIN_NRF_CSN, LOW);
    spi->beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
    spi->transfer(NRF24_W_TX_PAYLOAD);
    for (uint8_t i = 0; i < len; i++) {
        spi->transfer(buf[i]);
    }
    spi->endTransaction();
    digitalWrite(PIN_NRF_CSN, HIGH);
    return true;
}

bool nrf24_read_payload(uint8_t* buf, uint8_t len) {
    if (!buf || !nrf24_payload_length_valid(len))
        return false;
    digitalWrite(PIN_NRF_CSN, LOW);
    spi->beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
    spi->transfer(NRF24_R_RX_PAYLOAD);
    for (uint8_t i = 0; i < len; i++) {
        buf[i] = spi->transfer(0xFF);
    }
    spi->endTransaction();
    digitalWrite(PIN_NRF_CSN, HIGH);
    return true;
}

void nrf24_set_tx_mode() {
    digitalWrite(PIN_NRF_CE, LOW);
    nrf24_flush_tx();
    nrf24_write_reg(NRF24_REG_CONFIG, 0x02);  // PWR_UP | TX (No CRC)
    digitalWrite(PIN_NRF_CE, HIGH);
}

void nrf24_set_rx_mode() {
    digitalWrite(PIN_NRF_CE, LOW);
    nrf24_flush_rx();

    // NFE Silverware Bayang telemetry is transmitted without an XN297 CRC.
    // The nRF24 hardware CRC must therefore remain disabled or valid
    // Silverware telemetry will be discarded by the radio before the ESP32
    // can inspect the Bayang checksum.
    nrf24_write_reg(NRF24_REG_CONFIG, 0x03);  // PWR_UP | PRIM_RX, HW CRC disabled
    digitalWrite(PIN_NRF_CE, HIGH);
}

void nrf24_set_standby() {
    digitalWrite(PIN_NRF_CE, LOW);
    nrf24_write_reg(NRF24_REG_CONFIG, 0x00);  // PWR_DOWN
}

void nrf24_set_channel(uint8_t channel) {
    nrf24_write_reg(NRF24_REG_RF_CH, channel);
}

void nrf24_clear_irq_flags() {
    nrf24_write_reg(NRF24_REG_STATUS, NRF24_RX_DR | NRF24_TX_DS | NRF24_MAX_RT);
}

bool nrf24_irq_asserted() {
    return digitalRead(PIN_NRF_IRQ) == LOW;
}

uint8_t nrf24_get_status() {
    digitalWrite(PIN_NRF_CSN, LOW);
    spi->beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
    uint8_t status = spi->transfer(NRF24_NOP);
    spi->endTransaction();
    digitalWrite(PIN_NRF_CSN, HIGH);
    return status;
}
