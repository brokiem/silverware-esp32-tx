#pragma once
#include <stdbool.h>
#include <stdint.h>

enum class TelemetryProtocol : uint8_t {
    Original,
    ExtendedV1,
};

enum class FlightMode : uint8_t {
    Unknown,
    Acro,
    Level,
    Race,
    Horizon,
    RaceHorizon,
};

inline constexpr uint16_t TELEMETRY_EXTENDED_PAGE_CONTROL = 1U << 0;
inline constexpr uint16_t TELEMETRY_EXTENDED_PAGE_FLIGHT = 1U << 1;
inline constexpr uint16_t TELEMETRY_EXTENDED_PAGE_POWER = 1U << 2;
inline constexpr uint16_t TELEMETRY_EXTENDED_PAGE_SYSTEM = 1U << 3;
inline constexpr uint8_t TELEMETRY_SYSTEM_SUBPAGE_HEALTH = 1U << 0;
inline constexpr uint8_t TELEMETRY_SYSTEM_SUBPAGE_COUNTERS = 1U << 1;

struct TelemetryData {
    float batteryRawV;
    float batteryCompensatedV;
    bool lowVoltage;
    uint16_t receiverPacketsPerSecond;
    uint16_t pidP;
    uint16_t pidI;
    uint16_t pidD;
    TelemetryProtocol protocol;
    FlightMode flightMode;
    uint16_t extendedPagesSeen;
    uint8_t systemSubpagesSeen;
    uint8_t sequence;
    uint32_t telemetryPacketsLost;
    float rollDeg;
    float pitchDeg;
    float relativeYawDeg;
    float accelXG;
    float accelYG;
    float accelZG;
    float gyroRollDps;
    float gyroPitchDps;
    float gyroYawDps;
    float setpointRollDps;
    float setpointPitchDps;
    float setpointYawDps;
    bool armed;
    bool onGround;
    bool failsafe;
    bool idleUp;
    bool pidProfile;
    float commandedThrottlePercent;
    float appliedThrottlePercent;
    float motorOutputPercent[4];
    uint16_t flightTimeSeconds;
    int64_t lastFlightPageUs;
    uint8_t packetsLostPerSecond;
    uint8_t linkQualityPercent;
    float maximumRxGapMs;
    float currentRxGapMs;
    uint8_t failsafeCount;
    uint16_t loopTimeAverageUs;
    uint16_t loopTimeMaximumUs;
    uint16_t loopOverrunCount;
    int16_t imuTemperatureRaw;
    uint8_t imuType;
    uint8_t cpuLoadPercent;
    uint32_t telemetryTxCount;
    uint32_t receiverPacketTotal;
    uint32_t estimatedLostPacketTotal;
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
