#include "control_mapping.h"

#include <math.h>

#include "../config.h"

static float clamp_float(float value, float minimum, float maximum) {
    if (value < minimum)
        return minimum;
    if (value > maximum)
        return maximum;
    return value;
}

uint16_t map_trigger_throttle(int32_t raw_value) {
    if (raw_value < 0)
        return 0;
    if (raw_value > 1023)
        return 1023;
    return static_cast<uint16_t>(raw_value);
}

uint16_t map_half_stick_throttle(int32_t raw_axis_y, float deadband) {
    deadband = clamp_float(deadband, 0.0f, 0.99f);
    float normalized = clamp_float(-static_cast<float>(raw_axis_y) / 511.0f, -1.0f, 1.0f);

    if (normalized <= deadband)
        return 0;

    normalized = (normalized - deadband) / (1.0f - deadband);
    return static_cast<uint16_t>(clamp_float(normalized, 0.0f, 1.0f) * 1023.0f);
}

uint16_t map_bayang_channel(int32_t raw_value, bool is_throttle, bool reversed, float deadband, float expo) {
    if (is_throttle) {
        float normalized = clamp_float(static_cast<float>(raw_value) / 1023.0f, 0.0f, 1.0f);
        if (reversed)
            normalized = 1.0f - normalized;
        return static_cast<uint16_t>(normalized * 1023.0f * THROTTLE_MAX_PERCENT / 100.0f);
    }

    deadband = clamp_float(deadband, 0.0f, 0.99f);
    expo = clamp_float(expo, 0.0f, 1.0f);
    float normalized = clamp_float(static_cast<float>(raw_value) / 512.0f, -1.0f, 1.0f);
    if (reversed)
        normalized = -normalized;

    if (fabsf(normalized) < deadband) {
        normalized = 0.0f;
    } else {
        const float sign = normalized > 0.0f ? 1.0f : -1.0f;
        normalized = sign * ((fabsf(normalized) - deadband) / (1.0f - deadband));
    }

    normalized = expo * normalized * normalized * normalized + (1.0f - expo) * normalized;
    return static_cast<uint16_t>((clamp_float(normalized, -1.0f, 1.0f) + 1.0f) * 511.5f + 0.5f);
}
