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
        *state = {};
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
    controls->aux_rth = active && state.levelMode;
    controls->aux_picture = active && state.raceMode;
    controls->aux_video = active && state.horizonMode;
    controls->aux_headless = active && state.pidProfile;
    controls->aux_inverted = active && state.leds;
}

BayangControlState nfe_silverware_make_locked_control(bool gesture_enabled, uint16_t gesture_pitch) {
    BayangControlState controls = {};
    controls.roll = 512;
    controls.pitch = gesture_enabled ? gesture_pitch : 512;
    controls.yaw = 512;
    controls.throttle = 0;
    return controls;
}
