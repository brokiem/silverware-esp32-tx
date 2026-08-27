#include "pc_telemetry_state.h"

#include "../config.h"

namespace {

static_assert(STATE_BOOT == 0 && STATE_WAIT_GAMEPAD == 1 && STATE_BINDING == 2 && STATE_LOCKED == 3 &&
                  STATE_ACTIVE == 4 && STATE_GAMEPAD_FAILSAFE == 5 && STATE_RADIO_ERROR == 6,
              "SystemState wire values changed");

uint16_t age_ms(int64_t now_us, int64_t update_us, bool has_update) {
    if (!has_update)
        return PC_TELEMETRY_AGE_NEVER;
    int64_t age_us = now_us - update_us;
    if (age_us < 0)
        age_us = 0;
    const uint64_t milliseconds = static_cast<uint64_t>(age_us) / 1000U;
    return milliseconds > PC_TELEMETRY_AGE_MAX ? PC_TELEMETRY_AGE_MAX : static_cast<uint16_t>(milliseconds);
}

uint16_t elapsed_age_ms(int64_t age_us, bool has_update) {
    if (!has_update)
        return PC_TELEMETRY_AGE_NEVER;
    if (age_us < 0)
        age_us = 0;
    const uint64_t milliseconds = static_cast<uint64_t>(age_us) / 1000U;
    return milliseconds > PC_TELEMETRY_AGE_MAX ? PC_TELEMETRY_AGE_MAX : static_cast<uint16_t>(milliseconds);
}

uint16_t button_flags(const ControlState& controls) {
    uint16_t flags = 0;
    flags |= controls.btnA ? PC_TELEMETRY_BUTTON_A : 0;
    flags |= controls.btnB ? PC_TELEMETRY_BUTTON_B : 0;
    flags |= controls.btnX ? PC_TELEMETRY_BUTTON_X : 0;
    flags |= controls.btnY ? PC_TELEMETRY_BUTTON_Y : 0;
    flags |= controls.btnLB ? PC_TELEMETRY_BUTTON_LB : 0;
    flags |= controls.btnRB ? PC_TELEMETRY_BUTTON_RB : 0;
    flags |= controls.btnL3 ? PC_TELEMETRY_BUTTON_L3 : 0;
    flags |= controls.btnR3 ? PC_TELEMETRY_BUTTON_R3 : 0;
    flags |= controls.btnStart ? PC_TELEMETRY_BUTTON_START : 0;
    flags |= controls.btnView ? PC_TELEMETRY_BUTTON_VIEW : 0;
    flags |= controls.btnDPadUp ? PC_TELEMETRY_BUTTON_DPAD_UP : 0;
    flags |= controls.btnDPadDown ? PC_TELEMETRY_BUTTON_DPAD_DOWN : 0;
    flags |= controls.btnDPadLeft ? PC_TELEMETRY_BUTTON_DPAD_LEFT : 0;
    flags |= controls.btnDPadRight ? PC_TELEMETRY_BUTTON_DPAD_RIGHT : 0;
    return flags;
}

uint8_t aux_flags(const NfeSilverwareAuxState& aux) {
    uint8_t flags = 0;
    flags |= aux.levelMode ? PC_TELEMETRY_AUX_LEVEL : 0;
    flags |= aux.raceMode ? PC_TELEMETRY_AUX_RACE : 0;
    flags |= aux.horizonMode ? PC_TELEMETRY_AUX_HORIZON : 0;
    flags |= aux.pidProfile ? PC_TELEMETRY_AUX_PID_PROFILE : 0;
    flags |= aux.leds ? PC_TELEMETRY_AUX_LEDS : 0;
    return flags;
}

uint16_t status_flags(const PcTelemetryLocalStateInput& input, uint16_t gamepad_age) {
    uint16_t flags = 0;
    const bool gamepad_fresh = input.controls.connected && gamepad_age != PC_TELEMETRY_AGE_NEVER &&
                               gamepad_age <= GAMEPAD_TIMEOUT_MS;
    flags |= input.controls.connected ? PC_TELEMETRY_STATUS_GAMEPAD_CONNECTED : 0;
    flags |= gamepad_fresh ? PC_TELEMETRY_STATUS_GAMEPAD_FRESH : 0;
    flags |= input.systemState != STATE_ACTIVE ? PC_TELEMETRY_STATUS_SAFETY_LOCKED : 0;
    flags |= input.systemState == STATE_ACTIVE ? PC_TELEMETRY_STATUS_CONTROL_ENABLED : 0;
    flags |= input.systemState == STATE_BINDING ? PC_TELEMETRY_STATUS_BINDING : 0;
    flags |= input.systemState == STATE_GAMEPAD_FAILSAFE ? PC_TELEMETRY_STATUS_GAMEPAD_FAILSAFE : 0;
    flags |= input.systemState == STATE_RADIO_ERROR ? PC_TELEMETRY_STATUS_RADIO_ERROR : 0;
    flags |= input.radioInitialized ? PC_TELEMETRY_STATUS_RADIO_INITIALIZED : 0;
    flags |= input.telemetry.freshness != TelemetryFreshness::Never ? PC_TELEMETRY_STATUS_FC_TELEMETRY_SEEN : 0;
    flags |= input.telemetry.freshness == TelemetryFreshness::Fresh ? PC_TELEMETRY_STATUS_FC_TELEMETRY_FRESH : 0;
    flags |= input.telemetry.freshness == TelemetryFreshness::Stale ? PC_TELEMETRY_STATUS_FC_TELEMETRY_STALE : 0;
    return flags;
}

}  // namespace

PcTelemetryLocalState pc_telemetry_make_local_state(const PcTelemetryLocalStateInput& input) {
    PcTelemetryLocalState state = {};
    const uint16_t gamepad_age = age_ms(input.nowUs, input.controls.lastUpdateUs, input.controls.lastUpdateUs > 0);
    const uint16_t telemetry_age =
        elapsed_age_ms(input.telemetry.ageUs, input.telemetry.freshness != TelemetryFreshness::Never);

    state.systemState = static_cast<uint8_t>(input.systemState);
    state.statusFlags = status_flags(input, gamepad_age);
    state.buttons = button_flags(input.controls);
    state.auxModes = aux_flags(input.auxState);
    state.consecutiveTxFailures = input.consecutiveTxFailures;
    state.nextHoppingChannelIndex = input.nextHoppingChannelIndex & 0x03U;
    state.rollRaw = input.controls.rollRaw;
    state.pitchRaw = input.controls.pitchRaw;
    state.yawRaw = input.controls.yawRaw;
    state.throttleRaw = input.controls.throttleRaw;
    state.gamepadAgeMs = gamepad_age;
    state.fcTelemetryAgeMs = telemetry_age;
    state.txPackets = input.txPackets;
    state.txFailures = input.txFailures;
    state.telemetryAccepted = input.telemetryAccepted;
    state.telemetryRejected = input.telemetryRejected;
    state.deadlineMisses = input.deadlineMisses;
    state.exportQueueDrops = input.exportQueueDrops;
    return state;
}

bool pc_telemetry_local_state_due(int64_t now_us, int64_t* next_publish_us) {
    if (next_publish_us == nullptr || now_us < *next_publish_us)
        return false;
    *next_publish_us = now_us + PC_TELEMETRY_LOCAL_STATE_PERIOD_US;
    return true;
}
