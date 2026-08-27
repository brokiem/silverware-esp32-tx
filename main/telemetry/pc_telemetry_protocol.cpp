#include "pc_telemetry_protocol.h"

#include <string.h>

namespace {

void write_u16_be(uint8_t* output, uint16_t value) {
    output[0] = static_cast<uint8_t>(value >> 8);
    output[1] = static_cast<uint8_t>(value);
}

void write_u32_be(uint8_t* output, uint32_t value) {
    for (uint8_t index = 0; index < 4; ++index)
        output[index] = static_cast<uint8_t>(value >> ((3 - index) * 8));
}

void write_u64_be(uint8_t* output, uint64_t value) {
    for (uint8_t index = 0; index < 8; ++index)
        output[index] = static_cast<uint8_t>(value >> ((7 - index) * 8));
}

uint16_t read_u16_be(const uint8_t* input) {
    return static_cast<uint16_t>((static_cast<uint16_t>(input[0]) << 8) | input[1]);
}

uint32_t read_u32_be(const uint8_t* input) {
    uint32_t value = 0;
    for (uint8_t index = 0; index < 4; ++index)
        value = (value << 8) | input[index];
    return value;
}

uint64_t read_u64_be(const uint8_t* input) {
    uint64_t value = 0;
    for (uint8_t index = 0; index < 8; ++index)
        value = (value << 8) | input[index];
    return value;
}

size_t encode_message(uint8_t message_type, uint16_t sequence, uint64_t timestamp_us, const uint8_t* payload,
                      size_t payload_length, uint8_t* output, size_t output_capacity) {
    const size_t record_length = PC_TELEMETRY_ENVELOPE_SIZE + payload_length + PC_TELEMETRY_CRC_SIZE;
    if (payload == nullptr || output == nullptr || record_length > PC_TELEMETRY_LOCAL_STATE_RECORD_SIZE ||
        output_capacity < pc_telemetry_cobs_max_encoded_size(record_length) + 1)
        return 0;

    uint8_t record[PC_TELEMETRY_LOCAL_STATE_RECORD_SIZE] = {};
    record[0] = PC_TELEMETRY_PROTOCOL_VERSION;
    record[1] = message_type;
    write_u16_be(&record[2], static_cast<uint16_t>(payload_length));
    write_u16_be(&record[4], sequence);
    write_u64_be(&record[6], timestamp_us);
    memcpy(&record[PC_TELEMETRY_ENVELOPE_SIZE], payload, payload_length);
    const size_t crc_offset = PC_TELEMETRY_ENVELOPE_SIZE + payload_length;
    write_u16_be(&record[crc_offset], pc_telemetry_crc16_ccitt_false(record, crc_offset));

    const size_t encoded_length = pc_telemetry_cobs_encode(record, record_length, output, output_capacity - 1);
    if (encoded_length == 0)
        return 0;
    output[encoded_length] = 0;
    return encoded_length + 1;
}

bool decode_message(const uint8_t* frame, size_t frame_length, uint8_t expected_type, uint8_t* payload,
                    size_t expected_payload_length, uint16_t* sequence, uint64_t* timestamp_us) {
    if (frame == nullptr || payload == nullptr || sequence == nullptr || timestamp_us == nullptr || frame_length < 2 ||
        frame[frame_length - 1] != 0)
        return false;

    uint8_t record[PC_TELEMETRY_LOCAL_STATE_RECORD_SIZE] = {};
    size_t record_length = 0;
    if (!pc_telemetry_cobs_decode(frame, frame_length - 1, record, sizeof(record), &record_length))
        return false;
    const size_t expected_record_length =
        PC_TELEMETRY_ENVELOPE_SIZE + expected_payload_length + PC_TELEMETRY_CRC_SIZE;
    if (record_length != expected_record_length || record[0] != PC_TELEMETRY_PROTOCOL_VERSION ||
        record[1] != expected_type || read_u16_be(&record[2]) != expected_payload_length)
        return false;
    const size_t crc_offset = PC_TELEMETRY_ENVELOPE_SIZE + expected_payload_length;
    if (read_u16_be(&record[crc_offset]) != pc_telemetry_crc16_ccitt_false(record, crc_offset))
        return false;

    *sequence = read_u16_be(&record[4]);
    *timestamp_us = read_u64_be(&record[6]);
    memcpy(payload, &record[PC_TELEMETRY_ENVELOPE_SIZE], expected_payload_length);
    return true;
}

void serialize_local_state(const PcTelemetryLocalState& state, uint8_t* payload) {
    payload[0] = state.systemState;
    write_u16_be(&payload[1], state.statusFlags);
    write_u16_be(&payload[3], state.buttons);
    payload[5] = state.auxModes;
    payload[6] = state.consecutiveTxFailures;
    payload[7] = state.nextHoppingChannelIndex;
    write_u16_be(&payload[8], static_cast<uint16_t>(state.rollRaw));
    write_u16_be(&payload[10], static_cast<uint16_t>(state.pitchRaw));
    write_u16_be(&payload[12], static_cast<uint16_t>(state.yawRaw));
    write_u16_be(&payload[14], state.throttleRaw);
    write_u16_be(&payload[16], state.gamepadAgeMs);
    write_u16_be(&payload[18], state.fcTelemetryAgeMs);
    write_u32_be(&payload[20], state.txPackets);
    write_u32_be(&payload[24], state.txFailures);
    write_u32_be(&payload[28], state.telemetryAccepted);
    write_u32_be(&payload[32], state.telemetryRejected);
    write_u32_be(&payload[36], state.deadlineMisses);
    write_u32_be(&payload[40], state.exportQueueDrops);
}

void deserialize_local_state(const uint8_t* payload, PcTelemetryLocalState* state) {
    state->systemState = payload[0];
    state->statusFlags = read_u16_be(&payload[1]);
    state->buttons = read_u16_be(&payload[3]);
    state->auxModes = payload[5];
    state->consecutiveTxFailures = payload[6];
    state->nextHoppingChannelIndex = payload[7];
    state->rollRaw = static_cast<int16_t>(read_u16_be(&payload[8]));
    state->pitchRaw = static_cast<int16_t>(read_u16_be(&payload[10]));
    state->yawRaw = static_cast<int16_t>(read_u16_be(&payload[12]));
    state->throttleRaw = read_u16_be(&payload[14]);
    state->gamepadAgeMs = read_u16_be(&payload[16]);
    state->fcTelemetryAgeMs = read_u16_be(&payload[18]);
    state->txPackets = read_u32_be(&payload[20]);
    state->txFailures = read_u32_be(&payload[24]);
    state->telemetryAccepted = read_u32_be(&payload[28]);
    state->telemetryRejected = read_u32_be(&payload[32]);
    state->deadlineMisses = read_u32_be(&payload[36]);
    state->exportQueueDrops = read_u32_be(&payload[40]);
}

}  // namespace

void pc_telemetry_make_sample(PcTelemetrySample* sample, const uint8_t* packet, uint64_t timestamp_us,
                              uint16_t sequence) {
    if (sample == nullptr || packet == nullptr)
        return;
    sample->sequence = sequence;
    sample->timestampUs = timestamp_us;
    memcpy(sample->packet, packet, BAYANG_PACKET_SIZE);
}

uint16_t pc_telemetry_crc16_ccitt_false(const uint8_t* data, size_t length) {
    uint16_t crc = 0xFFFF;
    if (data == nullptr)
        return crc;
    for (size_t index = 0; index < length; ++index) {
        crc ^= static_cast<uint16_t>(data[index]) << 8;
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = crc & 0x8000 ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

size_t pc_telemetry_cobs_max_encoded_size(size_t input_length) {
    return input_length + input_length / 254 + 1;
}

size_t pc_telemetry_cobs_encode(const uint8_t* input, size_t input_length, uint8_t* output,
                                size_t output_capacity) {
    if ((input == nullptr && input_length != 0) || output == nullptr ||
        output_capacity < pc_telemetry_cobs_max_encoded_size(input_length))
        return 0;

    size_t read_index = 0;
    size_t write_index = 1;
    size_t code_index = 0;
    uint8_t code = 1;

    while (read_index < input_length) {
        if (input[read_index] == 0) {
            output[code_index] = code;
            code_index = write_index++;
            code = 1;
            ++read_index;
        } else {
            output[write_index++] = input[read_index++];
            if (++code == 0xFF) {
                output[code_index] = code;
                code_index = write_index++;
                code = 1;
            }
        }
    }
    output[code_index] = code;
    return write_index;
}

bool pc_telemetry_cobs_decode(const uint8_t* input, size_t input_length, uint8_t* output, size_t output_capacity,
                              size_t* output_length) {
    if (input == nullptr || output == nullptr || output_length == nullptr || input_length == 0)
        return false;

    size_t read_index = 0;
    size_t write_index = 0;
    while (read_index < input_length) {
        const uint8_t code = input[read_index++];
        if (code == 0 || read_index + code - 1 > input_length)
            return false;
        for (uint8_t index = 1; index < code; ++index) {
            if (write_index >= output_capacity)
                return false;
            output[write_index++] = input[read_index++];
        }
        if (code != 0xFF && read_index < input_length) {
            if (write_index >= output_capacity)
                return false;
            output[write_index++] = 0;
        }
    }
    *output_length = write_index;
    return true;
}

size_t pc_telemetry_encode_frame(const PcTelemetrySample& sample, uint8_t* output, size_t output_capacity) {
    return encode_message(PC_TELEMETRY_MESSAGE_RAW_BAYANG, sample.sequence, sample.timestampUs, sample.packet,
                          BAYANG_PACKET_SIZE, output, output_capacity);
}

bool pc_telemetry_decode_frame(const uint8_t* frame, size_t frame_length, PcTelemetrySample* sample) {
    if (sample == nullptr)
        return false;
    return decode_message(frame, frame_length, PC_TELEMETRY_MESSAGE_RAW_BAYANG, sample->packet, BAYANG_PACKET_SIZE,
                          &sample->sequence, &sample->timestampUs);
}

size_t pc_telemetry_encode_local_state_frame(const PcTelemetryLocalState& state, uint16_t sequence,
                                             uint64_t timestamp_us, uint8_t* output, size_t output_capacity) {
    uint8_t payload[PC_TELEMETRY_LOCAL_STATE_PAYLOAD_SIZE] = {};
    serialize_local_state(state, payload);
    return encode_message(PC_TELEMETRY_MESSAGE_LOCAL_STATE, sequence, timestamp_us, payload, sizeof(payload), output,
                          output_capacity);
}

bool pc_telemetry_decode_local_state_frame(const uint8_t* frame, size_t frame_length, PcTelemetryLocalState* state,
                                           uint16_t* sequence, uint64_t* timestamp_us) {
    if (state == nullptr)
        return false;
    uint8_t payload[PC_TELEMETRY_LOCAL_STATE_PAYLOAD_SIZE] = {};
    if (!decode_message(frame, frame_length, PC_TELEMETRY_MESSAGE_LOCAL_STATE, payload, sizeof(payload), sequence,
                        timestamp_us))
        return false;
    deserialize_local_state(payload, state);
    return true;
}
