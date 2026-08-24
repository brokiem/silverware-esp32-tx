#include "gamepad_manager.h"
#include "../config.h"
#include "../util/log.h"
#include <Bluepad32.h>
#include <esp_timer.h>
#include <math.h>

static ControllerPtr myController = nullptr;
static struct ControlState current_state = {};

static void onConnectedController(ControllerPtr ctl) {
    if (myController == nullptr) {
        myController = ctl;
        LOG("Gamepad connected");
    } else {
        LOG("Additional gamepad connected, ignoring");
    }
}

static void onDisconnectedController(ControllerPtr ctl) {
    if (myController == ctl) {
        myController = nullptr;
        LOG("Gamepad disconnected");
        current_state.connected = false;
    }
}

void gamepad_init() {
    BP32.setup(&onConnectedController, &onDisconnectedController);
    BP32.enableVirtualDevice(false);
    memset(&current_state, 0, sizeof(current_state));
}

void gamepad_update() {
    bool dataUpdated = BP32.update();
    if (myController && myController->isConnected() && dataUpdated) {
        current_state.rollRaw = myController->axisRX();
        current_state.pitchRaw = -myController->axisRY(); // Invert Y so up is positive
        current_state.yawRaw = myController->axisX();
        
        // Handle throttle mapping based on config
#if THROTTLE_SOURCE == THROTTLE_SRC_TRIGGER
        // RT is 0..1023 in Bluepad32
        current_state.throttleRaw = myController->throttle();
#elif THROTTLE_SOURCE == THROTTLE_SRC_LEFT_STICK_HALF
        // Use left stick Y axis (-512 to 511). Up is usually negative in Bluepad32, so -axisY().
        int16_t ly = -myController->axisY();
        
        // Convert to normalized float
        float norm_ly = (float)ly / 511.0f;
        
        // Apply deadband around the center so it doesn't drift when released
        if (norm_ly < STICK_DEADBAND) {
            norm_ly = 0.0f;
        } else {
            // Rescale so it starts at 0 right after the deadband
            norm_ly = (norm_ly - STICK_DEADBAND) / (1.0f - STICK_DEADBAND);
        }
        
        current_state.throttleRaw = (uint16_t)(norm_ly * 1023.0f);
        if (current_state.throttleRaw > 1023) {
            current_state.throttleRaw = 1023;
        }
#endif
        
        current_state.btnA = myController->a();
        current_state.btnB = myController->b();
        current_state.btnX = myController->x();
        current_state.btnY = myController->y();
        current_state.btnLB = myController->l1();
        current_state.btnRB = myController->r1();
        current_state.btnL3 = myController->thumbL();
        current_state.btnR3 = myController->thumbR();
        current_state.btnStart = myController->miscButtons() & 0x02; // options/start
        current_state.btnMenu = myController->miscButtons() & 0x04; // menu/select
        
        current_state.btnDPadUp = myController->dpad() & 0x01;
        current_state.btnDPadDown = myController->dpad() & 0x02;
        current_state.btnDPadRight = myController->dpad() & 0x04;
        current_state.btnDPadLeft = myController->dpad() & 0x08;

        current_state.connected = true;
        current_state.lastUpdateUs = esp_timer_get_time();
    }
}

void gamepad_get_state(struct ControlState* state) {
    memcpy(state, &current_state, sizeof(struct ControlState));
}

uint16_t gamepad_get_bayang_channel(int16_t raw_value, bool is_throttle, bool reversed, float deadband, float expo) {
    // raw_value is expected to be -512 to 511 for Bluepad32 axes, and 0..1023 for throttle
    // Let's normalize it to -1.0 to 1.0
    float normalized = 0.0f;
    if (is_throttle) {
        normalized = (float)raw_value / 1023.0f;
        if (normalized < 0.0f) normalized = 0.0f;
        if (normalized > 1.0f) normalized = 1.0f;
        
        if (reversed) {
            normalized = 1.0f - normalized;
        }
        
        // Throttle has no deadband/expo
        return (uint16_t)(normalized * 1023.0f);
    } else {
        normalized = (float)raw_value / 512.0f;
        if (normalized < -1.0f) normalized = -1.0f;
        if (normalized > 1.0f) normalized = 1.0f;
        
        if (reversed) {
            normalized = -normalized;
        }
        
        // Apply deadband
        if (fabs(normalized) < deadband) {
            normalized = 0.0f;
        } else {
            // Rescale after deadband
            float sign = (normalized > 0) ? 1.0f : -1.0f;
            normalized = sign * ((fabs(normalized) - deadband) / (1.0f - deadband));
        }
        
        // Apply expo
        if (expo > 0.01f) {
            normalized = expo * (normalized * normalized * normalized) + (1.0f - expo) * normalized;
        }
        
        // Map to 0..1023
        return (uint16_t)((normalized + 1.0f) * 511.5f);
    }
}
