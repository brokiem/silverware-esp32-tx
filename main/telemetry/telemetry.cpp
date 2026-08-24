#include "telemetry.h"
#include <esp_timer.h>
#include <string.h>

static struct TelemetryData current_telemetry = {};

void telemetry_init() {
    memset(&current_telemetry, 0, sizeof(current_telemetry));
}

void telemetry_parse(const uint8_t* packet, int64_t current_time_us) {
    if (packet[0] != 0x85) return;

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
    current_telemetry.receiverPacketsPerSecond = packet[7] * 2; // DeviationTX multiplies by 2

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
    current_telemetry.valid = true;
}

bool telemetry_get_data(struct TelemetryData* data) {
    // Return a coherent snapshot
    memcpy(data, &current_telemetry, sizeof(struct TelemetryData));
    return data->valid;
}
