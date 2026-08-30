#include "telemetry.h"
#include "../config.h"
#include "../radio/bayang.h"

namespace {

static TelemetryData current_telemetry = {};
static bool has_telemetry = false;
static bool has_sequence = false;
static uint8_t last_sequence = 0;

uint32_t read_bits(const uint8_t* packet, uint8_t* bit_offset, uint8_t bit_count) {
    uint32_t value = 0;
    for (uint8_t bit = 0; bit < bit_count; ++bit) {
        const uint8_t byte_index = 2 + (*bit_offset >> 3);
        const uint8_t byte_bit = 7 - (*bit_offset & 7);
        value = (value << 1) | ((packet[byte_index] >> byte_bit) & 1U);
        ++*bit_offset;
    }
    return value;
}

int32_t read_signed_bits(const uint8_t* packet, uint8_t* bit_offset, uint8_t bit_count) {
    uint32_t value = read_bits(packet, bit_offset, bit_count);
    const uint32_t sign_bit = 1UL << (bit_count - 1);
    if (value & sign_bit)
        value |= ~((1UL << bit_count) - 1UL);
    return static_cast<int32_t>(value);
}

float unit_value_to_percent(uint32_t value, uint8_t bits) {
    return value * 100.0f / static_cast<float>((1UL << bits) - 1UL);
}

FlightMode decode_flight_mode(uint8_t flags) {
    const bool level = flags & (1U << 3);
    const bool race = flags & (1U << 4);
    const bool horizon = flags & (1U << 5);
    if (!level)
        return FlightMode::Acro;
    if (race && horizon)
        return FlightMode::RaceHorizon;
    if (horizon)
        return FlightMode::Horizon;
    if (race)
        return FlightMode::Race;
    return FlightMode::Level;
}

void clear_extended_data() {
    current_telemetry.batteryRawV = 0.0f;
    current_telemetry.batteryCompensatedV = 0.0f;
    current_telemetry.lowVoltage = false;
    current_telemetry.receiverPacketsPerSecond = 0;
    current_telemetry.flightMode = FlightMode::Unknown;
    current_telemetry.extendedPagesSeen = 0;
    current_telemetry.systemSubpagesSeen = 0;
    current_telemetry.sequence = 0;
    current_telemetry.telemetryPacketsLost = 0;
    current_telemetry.rollDeg = 0.0f;
    current_telemetry.pitchDeg = 0.0f;
    current_telemetry.relativeYawDeg = 0.0f;
    current_telemetry.accelXG = 0.0f;
    current_telemetry.accelYG = 0.0f;
    current_telemetry.accelZG = 0.0f;
    current_telemetry.gyroRollDps = 0.0f;
    current_telemetry.gyroPitchDps = 0.0f;
    current_telemetry.gyroYawDps = 0.0f;
    current_telemetry.setpointRollDps = 0.0f;
    current_telemetry.setpointPitchDps = 0.0f;
    current_telemetry.setpointYawDps = 0.0f;
    current_telemetry.armed = false;
    current_telemetry.onGround = false;
    current_telemetry.failsafe = false;
    current_telemetry.idleUp = false;
    current_telemetry.pidProfile = false;
    current_telemetry.commandedThrottlePercent = 0.0f;
    current_telemetry.appliedThrottlePercent = 0.0f;
    for (float& motor : current_telemetry.motorOutputPercent)
        motor = 0.0f;
    current_telemetry.flightTimeSeconds = 0;
    current_telemetry.lastFlightPageUs = 0;
    current_telemetry.packetsLostPerSecond = 0;
    current_telemetry.linkQualityPercent = 0;
    current_telemetry.maximumRxGapMs = 0.0f;
    current_telemetry.currentRxGapMs = 0.0f;
    current_telemetry.failsafeCount = 0;
    current_telemetry.loopTimeAverageUs = 0;
    current_telemetry.loopTimeMaximumUs = 0;
    current_telemetry.loopOverrunCount = 0;
    current_telemetry.imuTemperatureRaw = 0;
    current_telemetry.imuType = 0;
    current_telemetry.cpuLoadPercent = 0;
    current_telemetry.telemetryTxCount = 0;
    current_telemetry.receiverPacketTotal = 0;
    current_telemetry.estimatedLostPacketTotal = 0;
    has_sequence = false;
}

void parse_control(const uint8_t* packet) {
    uint8_t offset = 0;
    float* gyro[] = {&current_telemetry.gyroRollDps, &current_telemetry.gyroPitchDps,
                     &current_telemetry.gyroYawDps};
    float* setpoint[] = {&current_telemetry.setpointRollDps, &current_telemetry.setpointPitchDps,
                        &current_telemetry.setpointYawDps};
    for (float* axis : gyro)
        *axis = read_signed_bits(packet, &offset, 10) * 4.0f;
    for (float* axis : setpoint)
        *axis = read_signed_bits(packet, &offset, 10) * 4.0f;
    current_telemetry.commandedThrottlePercent = unit_value_to_percent(read_bits(packet, &offset, 6), 6);
    current_telemetry.appliedThrottlePercent = unit_value_to_percent(read_bits(packet, &offset, 6), 6);
    for (float& motor : current_telemetry.motorOutputPercent)
        motor = unit_value_to_percent(read_bits(packet, &offset, 6), 6);
    current_telemetry.extendedPagesSeen |= TELEMETRY_EXTENDED_PAGE_CONTROL;
}

void parse_flight(const uint8_t* packet, int64_t current_time_us) {
    uint8_t offset = 0;
    current_telemetry.rollDeg = read_signed_bits(packet, &offset, 12) * 0.1f;
    current_telemetry.pitchDeg = read_signed_bits(packet, &offset, 12) * 0.1f;
    current_telemetry.relativeYawDeg = read_signed_bits(packet, &offset, 12) * 0.1f;
    current_telemetry.accelXG = read_signed_bits(packet, &offset, 12) / 256.0f;
    current_telemetry.accelYG = read_signed_bits(packet, &offset, 12) / 256.0f;
    current_telemetry.accelZG = read_signed_bits(packet, &offset, 12) / 256.0f;
    current_telemetry.flightTimeSeconds = read_bits(packet, &offset, 16);
    const uint8_t flags = read_bits(packet, &offset, 8);
    current_telemetry.onGround = flags & (1U << 0);
    current_telemetry.idleUp = flags & (1U << 1);
    current_telemetry.lowVoltage = flags & (1U << 2);
    current_telemetry.pidProfile = flags & (1U << 6);
    current_telemetry.flightMode = decode_flight_mode(flags);
    current_telemetry.lastFlightPageUs = current_time_us;
    current_telemetry.extendedPagesSeen |= TELEMETRY_EXTENDED_PAGE_FLIGHT;
}

void parse_power(const uint8_t* packet) {
    uint8_t offset = 0;
    current_telemetry.batteryRawV = read_bits(packet, &offset, 16) / 1000.0f;
    current_telemetry.batteryCompensatedV = read_bits(packet, &offset, 16) / 1000.0f;
    current_telemetry.receiverPacketsPerSecond = read_bits(packet, &offset, 8);
    current_telemetry.packetsLostPerSecond = read_bits(packet, &offset, 8);
    current_telemetry.linkQualityPercent = read_bits(packet, &offset, 8);
    const uint8_t flags = read_bits(packet, &offset, 8);
    current_telemetry.lowVoltage = flags & 1U;
    current_telemetry.maximumRxGapMs = read_bits(packet, &offset, 16) * 0.1f;
    current_telemetry.currentRxGapMs = read_bits(packet, &offset, 8) * 0.1f;
    current_telemetry.failsafeCount = read_bits(packet, &offset, 8);
    current_telemetry.extendedPagesSeen |= TELEMETRY_EXTENDED_PAGE_POWER;
}

void parse_system(const uint8_t* packet) {
    uint8_t offset = 0;
    const bool counters = read_bits(packet, &offset, 1);
    if (!counters) {
        current_telemetry.loopTimeAverageUs = read_bits(packet, &offset, 16);
        current_telemetry.loopTimeMaximumUs = read_bits(packet, &offset, 16);
        current_telemetry.loopOverrunCount = read_bits(packet, &offset, 16);
        current_telemetry.imuTemperatureRaw = read_signed_bits(packet, &offset, 16);
        current_telemetry.imuType = read_bits(packet, &offset, 8);
        current_telemetry.cpuLoadPercent = read_bits(packet, &offset, 8);
        current_telemetry.telemetryTxCount = read_bits(packet, &offset, 15);
        current_telemetry.systemSubpagesSeen |= TELEMETRY_SYSTEM_SUBPAGE_HEALTH;
    } else {
        current_telemetry.receiverPacketTotal = read_bits(packet, &offset, 32);
        current_telemetry.estimatedLostPacketTotal = read_bits(packet, &offset, 32);
        current_telemetry.telemetryTxCount = read_bits(packet, &offset, 31);
        current_telemetry.systemSubpagesSeen |= TELEMETRY_SYSTEM_SUBPAGE_COUNTERS;
    }
    current_telemetry.extendedPagesSeen |= TELEMETRY_EXTENDED_PAGE_SYSTEM;
}

void parse_extended(const uint8_t* packet, int64_t current_time_us) {
    const uint8_t common = packet[1];
    const uint8_t page = common >> 6;
    const uint8_t sequence = common & 0x0F;
    current_telemetry.armed = common & (1U << 5);
    current_telemetry.failsafe = common & (1U << 4);
    if (has_sequence) {
        const uint8_t expected = (last_sequence + 1) & 0x0F;
        current_telemetry.telemetryPacketsLost += (sequence - expected) & 0x0F;
    }
    current_telemetry.sequence = sequence;
    last_sequence = sequence;
    has_sequence = true;

    if (page == 0)
        parse_control(packet);
    else if (page == 1)
        parse_flight(packet, current_time_us);
    else if (page == 2)
        parse_power(packet);
    else
        parse_system(packet);
}

}  // namespace

void telemetry_init() {
    current_telemetry = {};
    current_telemetry.flightMode = FlightMode::Unknown;
    has_telemetry = false;
    has_sequence = false;
    last_sequence = 0;
}

bool telemetry_parse(const uint8_t* packet, int64_t current_time_us) {
    if (!packet || !bayang_check_telemetry(packet))
        return false;

    const TelemetryProtocol protocol = packet[0] == 0x86 ? TelemetryProtocol::ExtendedV1
                                                         : TelemetryProtocol::Original;
    if (current_telemetry.protocol != protocol) {
        clear_extended_data();
        current_telemetry.protocol = protocol;
    }

    if (protocol == TelemetryProtocol::ExtendedV1) {
        parse_extended(packet, current_time_us);
    } else {
        const uint16_t raw_centivolts = ((packet[3] & 0x07) << 8) | packet[4];
        const uint16_t comp_centivolts = ((packet[5] & 0x07) << 8) | packet[6];
        current_telemetry.batteryRawV = raw_centivolts / 100.0f;
        current_telemetry.batteryCompensatedV = comp_centivolts / 100.0f;
        current_telemetry.lowVoltage = ((packet[3] >> 3) & 1U);
        current_telemetry.receiverPacketsPerSecond = packet[7] * 2;

        const uint8_t pid_term = packet[8] >> 6;
        const uint16_t pid_value = ((packet[8] & 0x3F) << 8) | packet[9];
        if (pid_term == 0)
            current_telemetry.pidP = pid_value;
        else if (pid_term == 1)
            current_telemetry.pidI = pid_value;
        else if (pid_term == 2)
            current_telemetry.pidD = pid_value;
    }

    current_telemetry.lastValidUs = current_time_us;
    has_telemetry = true;
    return true;
}

TelemetrySnapshot telemetry_get_snapshot(int64_t current_time_us) {
    TelemetrySnapshot snapshot = {};
    snapshot.data = current_telemetry;
    if (!has_telemetry) {
        snapshot.freshness = TelemetryFreshness::Never;
        return snapshot;
    }

    snapshot.ageUs = current_time_us - current_telemetry.lastValidUs;
    if (snapshot.ageUs < 0)
        snapshot.ageUs = 0;
    snapshot.freshness = snapshot.ageUs <= static_cast<int64_t>(TELEMETRY_TIMEOUT_MS) * 1000
                             ? TelemetryFreshness::Fresh
                             : TelemetryFreshness::Stale;
    return snapshot;
}
