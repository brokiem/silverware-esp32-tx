#include "telemetry.h"
#include "../config.h"
#include "../radio/bayang.h"

static TelemetryData current_telemetry = {};
static bool has_telemetry = false;

void telemetry_init() {
    current_telemetry = {};
    has_telemetry = false;
}

bool telemetry_parse(const uint8_t* packet, int64_t current_time_us) {
    // Validate first so corrupt frames cannot refresh or partially change telemetry.
    if (!packet || !bayang_check_telemetry(packet))
        return false;

    // raw battery volts
    uint16_t rawCentivolts = ((packet[3] & 0x07) << 8) | packet[4];
    current_telemetry.batteryRawV = rawCentivolts / 100.0f;

    // compensated battery volts
    uint16_t compCentivolts = ((packet[5] & 0x07) << 8) | packet[6];
    current_telemetry.batteryCompensatedV = compCentivolts / 100.0f;

    // low voltage flag
    uint8_t flags = packet[3] >> 3;
    current_telemetry.lowVoltage = (flags & 1);

    // receiver packet rate
    current_telemetry.receiverPacketsPerSecond = packet[7] * 2;  // DeviationTX multiplies by 2

    // PID telemetry
    uint8_t pid_term = packet[8] >> 6;
    uint16_t pid_val = ((packet[8] & 0x3F) << 8) | packet[9];

    if (pid_term == 0) {
        current_telemetry.pidP = pid_val;
    } else if (pid_term == 1) {
        current_telemetry.pidI = pid_val;
    } else if (pid_term == 2) {
        current_telemetry.pidD = pid_val;
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
