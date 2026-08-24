#pragma once
#include <stdint.h>
#include <stdbool.h>

struct TelemetryData {
    float batteryRawV;
    float batteryCompensatedV;
    bool lowVoltage;
    uint16_t receiverPacketsPerSecond;
    uint16_t pidP;
    uint16_t pidI;
    uint16_t pidD;
    int64_t lastValidUs;
    bool valid;
};

void telemetry_init();
void telemetry_parse(const uint8_t* packet, int64_t current_time_us);
bool telemetry_get_data(struct TelemetryData* data);

