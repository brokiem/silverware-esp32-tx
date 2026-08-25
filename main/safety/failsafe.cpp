#include "failsafe.h"
#include "../config.h"

static enum SystemState current_state = STATE_BOOT;
static int64_t bind_start = 0;

void failsafe_init() {
    current_state = STATE_WAIT_GAMEPAD;
}

void failsafe_update_at(int64_t now,
                        bool gamepad_connected,
                        int64_t last_gamepad_update,
                        bool throttle_idle,
                        bool unlock_clicked,
                        bool bind_clicked,
                        bool disarm_clicked) {
    // Check timeouts
    bool gamepad_timeout = false;
    if (gamepad_connected) {
        if ((now - last_gamepad_update) > (static_cast<int64_t>(GAMEPAD_TIMEOUT_MS) * 1000)) {
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
            }
            break;

        case STATE_LOCKED:
            if (gamepad_timeout) {
                current_state = STATE_WAIT_GAMEPAD;
            } else if (bind_clicked && throttle_idle) {
                current_state = STATE_BINDING;
                bind_start = now;
            } else if (unlock_clicked && throttle_idle) {
                current_state = STATE_ACTIVE;
            }
            break;

        case STATE_BINDING:
            if (gamepad_timeout) {
                current_state = STATE_WAIT_GAMEPAD;
            } else if ((now - bind_start) > BIND_DURATION_US) {
                current_state = STATE_LOCKED;
            }
            break;

        case STATE_ACTIVE:
            if (gamepad_timeout) {
                current_state = STATE_GAMEPAD_FAILSAFE;
            } else if (disarm_clicked) {
                current_state = STATE_LOCKED;
            }
            break;

        case STATE_GAMEPAD_FAILSAFE:
            // Must go to LOCKED first when recovering
            if (!gamepad_timeout) {
                current_state = STATE_LOCKED;
            }
            break;

        case STATE_RADIO_ERROR:
            // One-way hardware fault: only a reboot may retry radio initialization.
            break;

        default:
            break;
    }
}

void failsafe_report_radio_error() {
    current_state = STATE_RADIO_ERROR;
}

enum SystemState failsafe_get_state() {
    return current_state;
}
