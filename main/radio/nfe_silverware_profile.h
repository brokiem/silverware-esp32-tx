#pragma once

#include "bayang.h"

struct NfeSilverwareAuxState {
    bool levelMode;
    bool raceMode;
    bool horizonMode;
    bool pidProfile;
    bool leds;

    bool previousA;
    bool previousX;
    bool previousY;
    bool previousRB;
    bool previousLB;
};

void nfe_silverware_update_aux(NfeSilverwareAuxState* state,
                               bool active,
                               bool button_a,
                               bool button_x,
                               bool button_y,
                               bool button_rb,
                               bool button_lb);
void nfe_silverware_apply_multi_aux(BayangControlState* controls, bool active, const NfeSilverwareAuxState& state);
BayangControlState nfe_silverware_make_locked_control(bool gesture_enabled,
                                                      uint16_t gesture_pitch,
                                                      const NfeSilverwareAuxState& state);
