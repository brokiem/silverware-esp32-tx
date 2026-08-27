#pragma once

#include <stdint.h>

#include "../gamepad/gamepad_manager.h"
#include "../radio/nfe_silverware_profile.h"
#include "../safety/failsafe.h"
#include "telemetry.h"
#include "pc_telemetry_protocol.h"

inline constexpr int64_t PC_TELEMETRY_LOCAL_STATE_PERIOD_US = 50000;
inline constexpr uint16_t PC_TELEMETRY_AGE_NEVER = 0xFFFF;
inline constexpr uint16_t PC_TELEMETRY_AGE_MAX = 0xFFFE;

struct PcTelemetryLocalStateInput {
    int64_t nowUs;
    SystemState systemState;
    ControlState controls;
    NfeSilverwareAuxState auxState;
    bool radioInitialized;
    uint8_t consecutiveTxFailures;
    uint8_t nextHoppingChannelIndex;
    TelemetrySnapshot telemetry;
    uint32_t txPackets;
    uint32_t txFailures;
    uint32_t telemetryAccepted;
    uint32_t telemetryRejected;
    uint32_t deadlineMisses;
    uint32_t exportQueueDrops;
};

PcTelemetryLocalState pc_telemetry_make_local_state(const PcTelemetryLocalStateInput& input);
bool pc_telemetry_local_state_due(int64_t now_us, int64_t* next_publish_us);
