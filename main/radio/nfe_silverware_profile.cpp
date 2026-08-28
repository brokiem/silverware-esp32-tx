#include "nfe_silverware_profile.h"

namespace {

void toggle_on_rising_edge(bool current, bool* previous, bool* value) {
    if (current && !*previous)
        *value = !*value;
    *previous = current;
}

}  // namespace

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
