#pragma once
#include <stdint.h>
#include <stdbool.h>

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
    bool btnMenu;
    bool btnDPadUp;
    bool btnDPadDown;
    bool btnDPadLeft;
    bool btnDPadRight;

    bool connected;
    int64_t lastUpdateUs;
};

void gamepad_init();
void gamepad_update(); // Must be called periodically from application task
void gamepad_get_state(struct ControlState* state);
uint16_t gamepad_get_bayang_channel(int16_t raw_value, bool is_throttle, bool reversed, float deadband, float expo);

