#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../radio/bayang.h"

inline constexpr uint8_t PC_TELEMETRY_PROTOCOL_VERSION = 1;
inline constexpr uint8_t PC_TELEMETRY_MESSAGE_RAW_BAYANG = 1;
inline constexpr uint8_t PC_TELEMETRY_MESSAGE_LOCAL_STATE = 2;
inline constexpr size_t PC_TELEMETRY_ENVELOPE_SIZE = 14;
inline constexpr size_t PC_TELEMETRY_CRC_SIZE = 2;
inline constexpr size_t PC_TELEMETRY_LOCAL_STATE_PAYLOAD_SIZE = 44;
inline constexpr size_t PC_TELEMETRY_RECORD_SIZE = PC_TELEMETRY_ENVELOPE_SIZE + BAYANG_PACKET_SIZE + PC_TELEMETRY_CRC_SIZE;
inline constexpr size_t PC_TELEMETRY_LOCAL_STATE_RECORD_SIZE =
    PC_TELEMETRY_ENVELOPE_SIZE + PC_TELEMETRY_LOCAL_STATE_PAYLOAD_SIZE + PC_TELEMETRY_CRC_SIZE;
inline constexpr size_t PC_TELEMETRY_MAX_ENCODED_SIZE = PC_TELEMETRY_LOCAL_STATE_RECORD_SIZE + 1;
inline constexpr size_t PC_TELEMETRY_MAX_FRAME_SIZE = PC_TELEMETRY_MAX_ENCODED_SIZE + 1;

enum PcTelemetryStatusFlag : uint16_t {
    PC_TELEMETRY_STATUS_GAMEPAD_CONNECTED = 1U << 0,
    PC_TELEMETRY_STATUS_GAMEPAD_FRESH = 1U << 1,
    PC_TELEMETRY_STATUS_SAFETY_LOCKED = 1U << 2,
    PC_TELEMETRY_STATUS_CONTROL_ENABLED = 1U << 3,
    PC_TELEMETRY_STATUS_BINDING = 1U << 4,
    PC_TELEMETRY_STATUS_GAMEPAD_FAILSAFE = 1U << 5,
    PC_TELEMETRY_STATUS_RADIO_ERROR = 1U << 6,
    PC_TELEMETRY_STATUS_RADIO_INITIALIZED = 1U << 7,
    PC_TELEMETRY_STATUS_FC_TELEMETRY_SEEN = 1U << 8,
    PC_TELEMETRY_STATUS_FC_TELEMETRY_FRESH = 1U << 9,
    PC_TELEMETRY_STATUS_FC_TELEMETRY_STALE = 1U << 10,
};

enum PcTelemetryButtonFlag : uint16_t {
    PC_TELEMETRY_BUTTON_A = 1U << 0,
    PC_TELEMETRY_BUTTON_B = 1U << 1,
    PC_TELEMETRY_BUTTON_X = 1U << 2,
    PC_TELEMETRY_BUTTON_Y = 1U << 3,
    PC_TELEMETRY_BUTTON_LB = 1U << 4,
    PC_TELEMETRY_BUTTON_RB = 1U << 5,
    PC_TELEMETRY_BUTTON_L3 = 1U << 6,
    PC_TELEMETRY_BUTTON_R3 = 1U << 7,
    PC_TELEMETRY_BUTTON_START = 1U << 8,
    PC_TELEMETRY_BUTTON_VIEW = 1U << 9,
    PC_TELEMETRY_BUTTON_DPAD_UP = 1U << 10,
    PC_TELEMETRY_BUTTON_DPAD_DOWN = 1U << 11,
    PC_TELEMETRY_BUTTON_DPAD_LEFT = 1U << 12,
    PC_TELEMETRY_BUTTON_DPAD_RIGHT = 1U << 13,
};

enum PcTelemetryAuxFlag : uint8_t {
    PC_TELEMETRY_AUX_LEVEL = 1U << 0,
    PC_TELEMETRY_AUX_RACE = 1U << 1,
    PC_TELEMETRY_AUX_HORIZON = 1U << 2,
    PC_TELEMETRY_AUX_PID_PROFILE = 1U << 3,
    PC_TELEMETRY_AUX_LEDS = 1U << 4,
};

struct PcTelemetrySample {
    uint16_t sequence;
    uint64_t timestampUs;
    uint8_t packet[BAYANG_PACKET_SIZE];
};

struct PcTelemetryLocalState {
    uint8_t systemState;
    uint16_t statusFlags;
    uint16_t buttons;
    uint8_t auxModes;
    uint8_t consecutiveTxFailures;
    uint8_t nextHoppingChannelIndex;
    int16_t rollRaw;
    int16_t pitchRaw;
    int16_t yawRaw;
    uint16_t throttleRaw;
    uint16_t gamepadAgeMs;
    uint16_t fcTelemetryAgeMs;
    uint32_t txPackets;
    uint32_t txFailures;
    uint32_t telemetryAccepted;
    uint32_t telemetryRejected;
    uint32_t deadlineMisses;
    uint32_t exportQueueDrops;
};

void pc_telemetry_make_sample(PcTelemetrySample* sample, const uint8_t* packet, uint64_t timestamp_us,
                              uint16_t sequence);
bool pc_telemetry_overlay_saved_flight_config(uint8_t* packet, uint8_t saved_aux_flags);
uint16_t pc_telemetry_crc16_ccitt_false(const uint8_t* data, size_t length);
size_t pc_telemetry_cobs_max_encoded_size(size_t input_length);
size_t pc_telemetry_cobs_encode(const uint8_t* input, size_t input_length, uint8_t* output,
                                size_t output_capacity);
bool pc_telemetry_cobs_decode(const uint8_t* input, size_t input_length, uint8_t* output, size_t output_capacity,
                              size_t* output_length);
size_t pc_telemetry_encode_frame(const PcTelemetrySample& sample, uint8_t* output, size_t output_capacity);
bool pc_telemetry_decode_frame(const uint8_t* frame, size_t frame_length, PcTelemetrySample* sample);
size_t pc_telemetry_encode_local_state_frame(const PcTelemetryLocalState& state, uint16_t sequence,
                                             uint64_t timestamp_us, uint8_t* output, size_t output_capacity);
bool pc_telemetry_decode_local_state_frame(const uint8_t* frame, size_t frame_length, PcTelemetryLocalState* state,
                                           uint16_t* sequence, uint64_t* timestamp_us);
