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

enum class NfeSilverwareGesture : uint8_t {
    None,
    Save,
    ToggleBindStorage,
    StickTravelOff,
    StickTravelOn,
    NextPidTerm,
    NextPidAxis,
    IncreasePid,
    DecreasePid,
    ToggleFeature1,
    ToggleLowVoltageLanding,
    TogglePropsDirection,
};

struct NfeSilverwareGesturePlayer {
    NfeSilverwareGesture command;
    NfeSilverwareGesture previousRequest;
    int64_t startedUs;
};

struct NfeSilverwareGestureOutput {
    bool active;
    uint16_t roll;
    uint16_t pitch;
};

uint8_t nfe_silverware_aux_flags(const NfeSilverwareAuxState& state);
void nfe_silverware_restore_aux(NfeSilverwareAuxState* state, uint8_t flags);
NfeSilverwareGestureOutput nfe_silverware_update_gesture(NfeSilverwareGesturePlayer* player,
                                                        NfeSilverwareGesture request,
                                                        int64_t now_us);
void nfe_silverware_cancel_gesture(NfeSilverwareGesturePlayer* player);

void nfe_silverware_update_aux(NfeSilverwareAuxState* state,
                               bool active,
                               bool button_a,
                               bool button_x,
                               bool button_y,
                               bool button_rb,
                               bool button_lb);
void nfe_silverware_apply_multi_aux(BayangControlState* controls, bool active, const NfeSilverwareAuxState& state);
BayangControlState nfe_silverware_make_locked_control(bool gesture_enabled,
                                                      uint16_t gesture_roll,
                                                      uint16_t gesture_pitch,
                                                      const NfeSilverwareAuxState& state);
