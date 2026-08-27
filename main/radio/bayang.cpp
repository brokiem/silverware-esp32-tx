#include "bayang.h"
#include <string.h>
#include "../config.h"

static uint8_t rx_tx_addr[BAYANG_ADDRESS_LENGTH];
static uint8_t hopping_channels[BAYANG_RF_CHANNELS];

static uint8_t calculate_checksum(const uint8_t* packet) {
    uint8_t sum = 0;
    for (int i = 0; i < BAYANG_PACKET_SIZE - 1; i++) {
        sum += packet[i];
    }
    return sum;
}

static uint8_t dynamic_trim(uint16_t v) {
    return (v >> 2) & 0xFC;
}

static uint16_t clamp_channel(uint16_t value) {
    return value > 1023 ? 1023 : value;
}

void bayang_init(const uint8_t* tx_id) {
    memcpy(rx_tx_addr, tx_id, BAYANG_ADDRESS_LENGTH);

    // Regular Bayang hopping channel calculation
    hopping_channels[0] = 0;
    hopping_channels[1] = (rx_tx_addr[3] & 0x1F) + 0x10;
    hopping_channels[2] = hopping_channels[1] + 0x20;
    hopping_channels[3] = hopping_channels[2] + 0x20;
}

void bayang_get_hopping_channels(uint8_t* channels) {
    memcpy(channels, hopping_channels, BAYANG_RF_CHANNELS);
}

uint8_t bayang_select_bind_header(bool telemetry_enabled, bool analog_aux_enabled) {
    if (analog_aux_enabled)
        return telemetry_enabled ? BAYANG_BIND_A1 : BAYANG_BIND_A2;
    return telemetry_enabled ? BAYANG_BIND_A3 : BAYANG_BIND_A4;
}

void bayang_build_bind_packet(uint8_t* packet, uint8_t bind_header) {
    memset(packet, 0, BAYANG_PACKET_SIZE);
    packet[0] = bind_header;
    memcpy(&packet[1], rx_tx_addr, BAYANG_ADDRESS_LENGTH);
    memcpy(&packet[6], hopping_channels, BAYANG_RF_CHANNELS);

    packet[10] = rx_tx_addr[0];
    packet[11] = rx_tx_addr[1];
    packet[12] = rx_tx_addr[2];

    packet[13] = 0x0A;  // Analog AUX disabled
    if (bind_header == BAYANG_BIND_A1 || bind_header == BAYANG_BIND_A2) {
        // We don't support transmitting a dynamic analog AUX channel here currently
        packet[13] = 0x00;
    }

    packet[14] = calculate_checksum(packet);
}

void bayang_build_data_packet(uint8_t* packet, const struct BayangControlState* state) {
    memset(packet, 0, BAYANG_PACKET_SIZE);

    packet[0] = BAYANG_DATA;
    packet[1] = 0xFA;  // Normal mode 0xFA

    packet[2] = 0;
    if (state->aux_flip)
        packet[2] |= BAYANG_FLAG_FLIP;
    if (state->aux_rth)
        packet[2] |= BAYANG_FLAG_RTH;
    if (state->aux_headless)
        packet[2] |= BAYANG_FLAG_HEADLESS;
    if (state->aux_picture)
        packet[2] |= BAYANG_FLAG_PICTURE;
    if (state->aux_video)
        packet[2] |= BAYANG_FLAG_VIDEO;

    packet[3] = 0;
    if (state->aux_take_off)
        packet[3] |= BAYANG_FLAG_TAKE_OFF;
    if (state->aux_inverted)
        packet[3] |= BAYANG_FLAG_INVERTED;
    if (state->aux_emg_stop)
        packet[3] |= BAYANG_FLAG_EMG_STOP;

    const bool headless = state->aux_headless;
    const uint16_t roll = clamp_channel(state->roll);
    const uint16_t pitch = clamp_channel(state->pitch);
    const uint16_t throttle = clamp_channel(state->throttle);
    const uint16_t yaw = clamp_channel(state->yaw);

    packet[4] = (roll >> 8) + (headless ? 0x7C : dynamic_trim(roll));
    packet[5] = roll & 0xFF;

    packet[6] = (pitch >> 8) + (headless ? 0x7C : dynamic_trim(pitch));
    packet[7] = pitch & 0xFF;

    packet[8] = (throttle >> 8) + 0x7C;
    packet[9] = throttle & 0xFF;

    packet[10] = (yaw >> 8) + (headless ? 0x7C : dynamic_trim(yaw));
    packet[11] = yaw & 0xFF;

    packet[12] = rx_tx_addr[2];
    packet[13] = 0x0A;  // Analog AUX disabled

    packet[14] = calculate_checksum(packet);
}

bool bayang_check_telemetry(const uint8_t* packet) {
    if (!packet || packet[0] != 0x85)
        return false;

    // Original NFE Silverware Bayang telemetry has no RF CRC. Its protocol
    // integrity check is the 8-bit additive checksum in byte 14.
    if (calculate_checksum(packet) != packet[14])
        return false;

    return true;
}
