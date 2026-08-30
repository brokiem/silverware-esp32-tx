#pragma once

#include <stdint.h>

#include "../radio/nfe_silverware_profile.h"
#include "../telemetry/telemetry.h"

bool prearm_configuration_confirmed(const TelemetrySnapshot& telemetry,
                                     const NfeSilverwareAuxState& aux_state,
                                     int64_t prearm_started_us);
