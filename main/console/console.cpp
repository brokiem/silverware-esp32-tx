#include "console.h"
#include "../telemetry/telemetry.h"
#include "../safety/failsafe.h"
#include "../gamepad/gamepad_manager.h"
#include "../config.h"
#include "../util/log.h"
#include <stdio.h>
#include <esp_timer.h>

static int64_t last_print = 0;

void console_init() {
    // Console is handled by Bluepad32 / ESP-IDF logging
}

void console_update() {
    int64_t now = esp_timer_get_time();
    if ((now - last_print) > (1000000 / STATUS_PRINT_HZ)) {
        last_print = now;
        
        enum SystemState state = failsafe_get_state();
        const char* state_str = "UNKNOWN";
        switch (state) {
            case STATE_BOOT:             state_str = "BOOT"; break;
            case STATE_WAIT_GAMEPAD:    state_str = "WAIT_GAMEPAD"; break;
            case STATE_BINDING:         state_str = "BINDING"; break;
            case STATE_LOCKED:          state_str = "LOCKED"; break;
            case STATE_ACTIVE:          state_str = "ACTIVE"; break;
            case STATE_GAMEPAD_FAILSAFE:state_str = "FAILSAFE"; break;
            case STATE_RADIO_ERROR:     state_str = "RADIO_ERR"; break;
        }
        
        struct TelemetryData tdata;
        bool has_telem = telemetry_get_data(&tdata);
        
        struct ControlState cstate;
        gamepad_get_state(&cstate);
        
        print_timestamp();
        printf("[STATE: %s] | Gamepad: %s | T: %d, R: %d, P: %d, Y: %d", 
            state_str, 
            cstate.connected ? "CON" : "DIS",
            cstate.throttleRaw, cstate.rollRaw, cstate.pitchRaw, cstate.yawRaw);
            
        if (has_telem) {
            int64_t age = (now - tdata.lastValidUs) / 1000;
            printf(" | TELEM (%lld ms): %.2fV %d pkts/s", (long long)age, tdata.batteryCompensatedV, tdata.receiverPacketsPerSecond);
        } else {
            printf(" | TELEM: None");
        }
        
        printf("\n");
    }
}
