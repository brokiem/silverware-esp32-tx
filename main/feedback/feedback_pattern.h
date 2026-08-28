#pragma once

#include <stdint.h>

#include "../safety/failsafe.h"

struct FeedbackOutput {
    bool ledOn;
    bool buzzerOn;
};

// Returns the output levels for a state transition without blocking the RF task.
FeedbackOutput feedback_pattern(SystemState previous, SystemState current, uint32_t elapsed_ms);
