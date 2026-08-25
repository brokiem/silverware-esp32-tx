#pragma once
#include <stdbool.h>
#include <stdint.h>

struct TelemetryData {
    float batteryRawV;
    float batteryCompensatedV;
    bool lowVoltage;
    uint16_t receiverPacketsPerSecond;
    uint16_t pidP;
    uint16_t pidI;
    uint16_t pidD;
    int64_t lastValidUs;
};

enum class TelemetryFreshness : uint8_t {
    Never,
    Fresh,
    Stale,
};

struct TelemetrySnapshot {
    TelemetryData data;
    TelemetryFreshness freshness;
    int64_t ageUs;
};

void telemetry_init();
bool telemetry_parse(const uint8_t* packet, int64_t current_time_us);
TelemetrySnapshot telemetry_get_snapshot(int64_t current_time_us);
