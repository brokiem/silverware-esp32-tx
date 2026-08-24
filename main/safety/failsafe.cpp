#include "failsafe.h"
#include "../config.h"
#include "../util/log.h"
#include <esp_timer.h>

static enum SystemState current_state = STATE_BOOT;
static int64_t bind_start = 0;

void failsafe_init() {
    current_state = STATE_WAIT_GAMEPAD;
}

void failsafe_update(bool gamepad_connected, int64_t last_gamepad_update, bool throttle_idle, bool unlock_clicked, bool bind_clicked, bool disarm_clicked) {
    int64_t now = esp_timer_get_time();
    
    // Check timeouts
    bool gamepad_timeout = false;
    if (gamepad_connected) {
        if ((now - last_gamepad_update) > ((int64_t)GAMEPAD_TIMEOUT_MS * 1000)) {
            gamepad_timeout = true;
        }
    } else {
        gamepad_timeout = true;
    }
    
    // State machine
    switch (current_state) {
        case STATE_WAIT_GAMEPAD:
            if (!gamepad_timeout) {
                current_state = STATE_LOCKED;
                LOG("Gamepad connected, STATE_LOCKED (Disarmed)");
            }
            break;
            
        case STATE_LOCKED:
            if (gamepad_timeout) {
                current_state = STATE_WAIT_GAMEPAD;
                LOG("Gamepad disconnected, STATE_WAIT_GAMEPAD");
            } else if (bind_clicked && throttle_idle) {
                current_state = STATE_BINDING;
                bind_start = now;
                LOG("Starting BIND mode (2s)...");
            } else if (unlock_clicked && throttle_idle) {
                current_state = STATE_ACTIVE;
                LOG("Armed & Unlocked! STATE_ACTIVE");
            }
            break;
            
        case STATE_BINDING:
            if (gamepad_timeout) {
                current_state = STATE_WAIT_GAMEPAD;
                LOG("Gamepad disconnected during bind, STATE_WAIT_GAMEPAD");
            } else if ((now - bind_start) > 2000000) { // 2 seconds bind duration
                current_state = STATE_LOCKED;
                LOG("Bind complete, returned to STATE_LOCKED");
            }
            break;
            
        case STATE_ACTIVE:
            if (gamepad_timeout) {
                current_state = STATE_GAMEPAD_FAILSAFE;
                LOG("Gamepad timeout! STATE_GAMEPAD_FAILSAFE");
            } else if (disarm_clicked) {
                current_state = STATE_LOCKED;
                LOG("Disarmed by button press, STATE_LOCKED");
            }
            break;
            
        case STATE_GAMEPAD_FAILSAFE:
            // Must go to LOCKED first when recovering
            if (!gamepad_timeout) {
                current_state = STATE_LOCKED;
                LOG("Gamepad recovered, STATE_LOCKED");
            }
            break;
            
        case STATE_RADIO_ERROR:
            // Requires reboot
            break;
            
        default:
            break;
    }
}

enum SystemState failsafe_get_state() {
    return current_state;
}
