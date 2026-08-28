#pragma once

#include "../safety/failsafe.h"

// Starts the low-priority feedback task and applies the initial state cue.
bool feedback_init(SystemState initial_state);

// Publishes a state transition without waiting for a cue to finish.
void feedback_notify_state(SystemState previous, SystemState current);
