#pragma once
#include <stdbool.h>
#include <stdint.h>

enum SystemState {
    STATE_BOOT = 0,
    STATE_WAIT_GAMEPAD,
    STATE_BINDING,
    STATE_LOCKED,
    STATE_ACTIVE,
    STATE_GAMEPAD_FAILSAFE,
    STATE_RADIO_ERROR,
    STATE_PREARM_MODE
};

void failsafe_init();
void failsafe_update_at(int64_t now,
                        bool gamepad_connected,
                        int64_t last_gamepad_update,
                        bool throttle_idle,
                        bool unlock_clicked,
                        bool bind_clicked,
                        bool disarm_clicked);
void failsafe_report_radio_error();
void failsafe_complete_prearm();
void failsafe_cancel_prearm();
enum SystemState failsafe_get_state();
