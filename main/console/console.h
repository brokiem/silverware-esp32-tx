#pragma once

#include <stdint.h>

#include "../gamepad/gamepad_manager.h"
#include "../safety/failsafe.h"
#include "../telemetry/telemetry.h"

struct RadioStats {
    uint32_t txPackets;
    uint32_t txFailures;
    uint32_t telemetryAccepted;
    uint32_t telemetryRejected;
    uint32_t deadlineMisses;
};

struct ConsoleStatus {
    int64_t timestampUs;
    SystemState state;
    ControlState controls;
    TelemetrySnapshot telemetry;
    RadioStats radio;
};

enum class ConsoleEventType : uint8_t {
    StateChanged,
    GamepadConnected,
    GamepadDisconnected,
    RadioInitFailed,
    RadioRuntimeFailed,
};

struct ConsoleEvent {
    ConsoleEventType type;
    SystemState previousState;
    SystemState currentState;
};

bool console_init();
void console_publish_status(const ConsoleStatus& status);
void console_publish_event(const ConsoleEvent& event);
