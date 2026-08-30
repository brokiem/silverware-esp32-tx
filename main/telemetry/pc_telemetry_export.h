#pragma once

#include <stdint.h>

#include "pc_telemetry_protocol.h"

bool pc_telemetry_export_init();
void pc_telemetry_export_publish_bayang(const uint8_t* packet, int64_t timestamp_us,
                                        bool overlay_saved_flight_config, uint8_t saved_aux_flags);
void pc_telemetry_export_publish_local_state(const PcTelemetryLocalState& state, int64_t timestamp_us);
uint32_t pc_telemetry_export_drop_count();
