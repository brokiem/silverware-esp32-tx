#pragma once

#include <stdint.h>

uint16_t map_trigger_throttle(int32_t raw_value);
uint16_t map_half_stick_throttle(int32_t raw_axis_y, float deadband);
uint16_t map_bayang_channel(int32_t raw_value, bool is_throttle, bool reversed, float deadband, float expo);
