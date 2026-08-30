#include "feedback_pattern.h"

namespace {

bool pulse(uint32_t elapsed_ms, uint32_t period_ms, uint32_t on_ms) {
    return (elapsed_ms % period_ms) < on_ms;
}

bool in_window(uint32_t elapsed_ms, uint32_t start_ms, uint32_t end_ms) {
    return elapsed_ms >= start_ms && elapsed_ms < end_ms;
}

}  // namespace

FeedbackOutput feedback_pattern(SystemState previous, SystemState current, uint32_t elapsed_ms) {
    FeedbackOutput output = {};

    switch (current) {
        case STATE_BOOT:
            output.ledOn = true;
            output.buzzerOn = elapsed_ms < 80;
            break;

        case STATE_WAIT_GAMEPAD:
            output.ledOn = pulse(elapsed_ms, 1000, 80);
            // One short chirp at startup or when a controller is lost while locked.
            output.buzzerOn = elapsed_ms < 80;
            break;

        case STATE_BINDING:
            output.ledOn = pulse(elapsed_ms, 200, 100);
            output.buzzerOn = in_window(elapsed_ms, 0, 70) || in_window(elapsed_ms, 140, 210);
            break;

        case STATE_LOCKED:
            output.ledOn = pulse(elapsed_ms, 1000, 500);
            if (previous == STATE_BINDING) {
                // Three quick chirps confirm that the binding window completed.
                output.buzzerOn = in_window(elapsed_ms, 0, 60) || in_window(elapsed_ms, 120, 180) ||
                                  in_window(elapsed_ms, 240, 300);
            } else if (previous == STATE_ACTIVE) {
                // A double chirp confirms an intentional disarm.
                output.buzzerOn = in_window(elapsed_ms, 0, 70) || in_window(elapsed_ms, 140, 210);
            } else {
                output.buzzerOn = elapsed_ms < 120;
            }
            break;

        case STATE_ACTIVE:
            output.ledOn = true;
            output.buzzerOn = elapsed_ms < 200;
            break;

        case STATE_PREARM_MODE:
            output.ledOn = pulse(elapsed_ms, 200, 100);
            output.buzzerOn = elapsed_ms < 80;
            break;

        case STATE_GAMEPAD_FAILSAFE: {
            output.ledOn = pulse(elapsed_ms, 200, 100);
            const uint32_t phase_ms = elapsed_ms % 1200;
            output.buzzerOn = in_window(phase_ms, 0, 80) || in_window(phase_ms, 160, 240) ||
                              in_window(phase_ms, 320, 400);
            break;
        }

        case STATE_RADIO_ERROR: {
            const uint32_t phase_ms = elapsed_ms % 1000;
            output.ledOn = in_window(phase_ms, 0, 100) || in_window(phase_ms, 200, 300);
            output.buzzerOn = in_window(phase_ms, 0, 200) || in_window(phase_ms, 400, 600);
            break;
        }
    }

    return output;
}
