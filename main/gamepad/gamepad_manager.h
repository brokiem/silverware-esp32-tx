#pragma once
#include <stdbool.h>
#include <stdint.h>

struct ControlState {
    int16_t rollRaw;
    int16_t pitchRaw;
    int16_t yawRaw;
    uint16_t throttleRaw;

    bool btnA;
    bool btnB;
    bool btnX;
    bool btnY;
    bool btnLB;
    bool btnRB;
    bool btnL3;
    bool btnR3;
    bool btnStart;
    bool btnView;
    bool btnDPadUp;
    bool btnDPadDown;
    bool btnDPadLeft;
    bool btnDPadRight;

    bool connected;
    int64_t lastUpdateUs;
};

void gamepad_init();
// Update and snapshot access must stay on the single-owner control/RF task.
void gamepad_update();
void gamepad_get_state(ControlState* state);
uint16_t gamepad_get_bayang_channel(int32_t raw_value, bool is_throttle, bool reversed, float deadband, float expo);
