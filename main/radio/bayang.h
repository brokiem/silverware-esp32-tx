#pragma once
#include <stdint.h>
#include <stdbool.h>

#define BAYANG_PACKET_SIZE 15
#define BAYANG_ADDRESS_LENGTH 5
#define BAYANG_RF_CHANNELS 4

// Bayang packet headers
#define BAYANG_BIND_A1 0xA1 // telemetry on, analog aux on
#define BAYANG_BIND_A2 0xA2 // telemetry off, analog aux on
#define BAYANG_BIND_A3 0xA3 // telemetry on, analog aux off (Default)
#define BAYANG_BIND_A4 0xA4 // telemetry off, analog aux off
#define BAYANG_DATA    0xA5

// Flags packet[2]
#define BAYANG_FLAG_FLIP     0x08
#define BAYANG_FLAG_RTH      0x10
#define BAYANG_FLAG_HEADLESS 0x20
#define BAYANG_FLAG_PICTURE  0x40
#define BAYANG_FLAG_VIDEO    0x80

// Flags packet[3]
#define BAYANG_FLAG_TAKE_OFF 0x20
#define BAYANG_FLAG_INVERTED 0x80
#define BAYANG_FLAG_EMG_STOP (0x04 | 0x08)

struct BayangControlState {
    uint16_t roll;
    uint16_t pitch;
    uint16_t throttle;
    uint16_t yaw;
    
    bool aux_flip;
    bool aux_rth;
    bool aux_headless;
    bool aux_picture;
    bool aux_video;
    
    bool aux_take_off;
    bool aux_inverted;
    bool aux_emg_stop;
};

void bayang_init(const uint8_t* tx_id);
void bayang_get_hopping_channels(uint8_t* channels);
void bayang_build_bind_packet(uint8_t* packet, uint8_t bind_header);
void bayang_build_data_packet(uint8_t* packet, const struct BayangControlState* state);
bool bayang_check_telemetry(const uint8_t* packet);

