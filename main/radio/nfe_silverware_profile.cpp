#include "nfe_silverware_profile.h"

namespace {

// The FC accepts a direction after 100 ms, rejects it as "long" after 500 ms,
// and requires more than 700 ms centered before a new sequence. These values
// leave useful margin for control-packet loss at both thresholds without
// making shortcuts feel sluggish.
constexpr int64_t GESTURE_IDLE_US = 800000;
constexpr int64_t GESTURE_STEP_US = 200000;
constexpr uint16_t GESTURE_LOW = 0;
constexpr uint16_t GESTURE_CENTER = 512;
constexpr uint16_t GESTURE_HIGH = 1023;

void toggle_on_rising_edge(bool current, bool* previous, bool* value) {
    if (current && !*previous)
        *value = !*value;
    *previous = current;
}

void gesture_directions(NfeSilverwareGesture command, int8_t directions[3]) {
    switch (command) {
        case NfeSilverwareGesture::Save: directions[0] = directions[1] = directions[2] = -2; break;
        case NfeSilverwareGesture::ToggleBindStorage: directions[0] = directions[1] = directions[2] = 2; break;
        case NfeSilverwareGesture::StickTravelOff: directions[0] = directions[1] = -1; directions[2] = -2; break;
        case NfeSilverwareGesture::StickTravelOn: directions[0] = directions[1] = 1; directions[2] = -2; break;
        case NfeSilverwareGesture::NextPidTerm: directions[0] = 2; directions[1] = -2; directions[2] = 2; break;
        case NfeSilverwareGesture::NextPidAxis: directions[0] = 2; directions[1] = directions[2] = -2; break;
        case NfeSilverwareGesture::IncreasePid: directions[0] = 2; directions[1] = -2; directions[2] = 1; break;
        case NfeSilverwareGesture::DecreasePid: directions[0] = 2; directions[1] = -2; directions[2] = -1; break;
        case NfeSilverwareGesture::ToggleFeature1: directions[0] = directions[1] = directions[2] = 1; break;
        case NfeSilverwareGesture::ToggleLowVoltageLanding: directions[0] = directions[1] = directions[2] = -1; break;
        case NfeSilverwareGesture::TogglePropsDirection: directions[0] = -2; directions[1] = 2; directions[2] = -2; break;
        case NfeSilverwareGesture::None: directions[0] = directions[1] = directions[2] = 0; break;
    }
}

}  // namespace

NfeSilverwareGestureOutput nfe_silverware_update_gesture(NfeSilverwareGesturePlayer* player,
                                                        NfeSilverwareGesture request,
                                                        int64_t now_us) {
    NfeSilverwareGestureOutput output = {false, GESTURE_CENTER, GESTURE_CENTER};
    if (player == nullptr)
        return output;

    if (player->command == NfeSilverwareGesture::None && request != NfeSilverwareGesture::None &&
        player->previousRequest == NfeSilverwareGesture::None) {
        player->command = request;
        player->startedUs = now_us;
    }
    player->previousRequest = request;

    if (player->command == NfeSilverwareGesture::None)
        return output;

    const int64_t elapsed = now_us - player->startedUs;
    const int64_t total_us = GESTURE_IDLE_US + 6 * GESTURE_STEP_US;
    if (elapsed < 0 || elapsed >= total_us) {
        player->command = NfeSilverwareGesture::None;
        return output;
    }

    output.active = true;
    if (elapsed < GESTURE_IDLE_US)
        return output;

    const int step = static_cast<int>((elapsed - GESTURE_IDLE_US) / GESTURE_STEP_US);
    if (step & 1)
        return output;

    int8_t directions[3] = {};
    gesture_directions(player->command, directions);
    const int8_t direction = directions[step / 2];
    if (direction == -1)
        output.roll = GESTURE_LOW;
    else if (direction == 1)
        output.roll = GESTURE_HIGH;
    else if (direction == -2)
        output.pitch = GESTURE_LOW;
    else if (direction == 2)
        output.pitch = GESTURE_HIGH;
    return output;
}

void nfe_silverware_cancel_gesture(NfeSilverwareGesturePlayer* player) {
    if (player != nullptr)
        *player = {};
}

uint8_t nfe_silverware_aux_flags(const NfeSilverwareAuxState& state) {
    return (state.levelMode ? 1U << 0 : 0) | (state.raceMode ? 1U << 1 : 0) |
           (state.horizonMode ? 1U << 2 : 0) | (state.pidProfile ? 1U << 3 : 0) |
           (state.leds ? 1U << 4 : 0);
}

void nfe_silverware_restore_aux(NfeSilverwareAuxState* state, uint8_t flags) {
    if (state == nullptr)
        return;
    *state = {};
    state->levelMode = flags & (1U << 0);
    state->raceMode = flags & (1U << 1);
    state->horizonMode = flags & (1U << 2);
    state->pidProfile = flags & (1U << 3);
    state->leds = flags & (1U << 4);
}

void nfe_silverware_update_aux(NfeSilverwareAuxState* state,
                               bool active,
                               bool button_a,
                               bool button_x,
                               bool button_y,
                               bool button_rb,
                               bool button_lb) {
    if (!active) {
        // Preserve the selected FC configuration while controls are locked.
        // Button edges are intentionally left unchanged so a button held
        // during a disarm cannot toggle its selection again on re-arm.
        return;
    }

    toggle_on_rising_edge(button_a, &state->previousA, &state->levelMode);
    toggle_on_rising_edge(button_x, &state->previousX, &state->raceMode);
    toggle_on_rising_edge(button_y, &state->previousY, &state->horizonMode);
    toggle_on_rising_edge(button_rb, &state->previousRB, &state->pidProfile);
    toggle_on_rising_edge(button_lb, &state->previousLB, &state->leds);
}

void nfe_silverware_apply_multi_aux(BayangControlState* controls, bool active, const NfeSilverwareAuxState& state) {
    // rajawali config.h: CH5 arms/enables idle-up; CH6..CH10 select FC modes.
    controls->aux_flip = active;
    controls->aux_rth = state.levelMode;
    controls->aux_picture = state.raceMode;
    controls->aux_video = state.horizonMode;
    controls->aux_headless = state.pidProfile;
    controls->aux_inverted = state.leds;
}

BayangControlState nfe_silverware_make_locked_control(bool gesture_enabled,
                                                      uint16_t gesture_roll,
                                                      uint16_t gesture_pitch,
                                                      const NfeSilverwareAuxState& state) {
    BayangControlState controls = {};
    controls.roll = gesture_enabled ? gesture_roll : 512;
    controls.pitch = gesture_enabled ? gesture_pitch : 512;
    controls.yaw = 512;
    controls.throttle = 0;
    nfe_silverware_apply_multi_aux(&controls, false, state);
    return controls;
}
