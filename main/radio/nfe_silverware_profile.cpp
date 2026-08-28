#include "nfe_silverware_profile.h"

namespace {

void toggle_on_rising_edge(bool current, bool* previous, bool* value) {
    if (current && !*previous)
        *value = !*value;
    *previous = current;
}

}  // namespace

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
                                                      uint16_t gesture_pitch,
                                                      const NfeSilverwareAuxState& state) {
    BayangControlState controls = {};
    controls.roll = 512;
    controls.pitch = gesture_enabled ? gesture_pitch : 512;
    controls.yaw = 512;
    controls.throttle = 0;
    nfe_silverware_apply_multi_aux(&controls, false, state);
    return controls;
}
