#include "prearm_guard.h"

namespace {

FlightMode selected_flight_mode(const NfeSilverwareAuxState& aux_state) {
    if (!aux_state.levelMode)
        return FlightMode::Acro;
    if (aux_state.raceMode && aux_state.horizonMode)
        return FlightMode::RaceHorizon;
    if (aux_state.horizonMode)
        return FlightMode::Horizon;
    if (aux_state.raceMode)
        return FlightMode::Race;
    return FlightMode::Level;
}

}  // namespace

bool prearm_configuration_confirmed(const TelemetrySnapshot& telemetry,
                                     const NfeSilverwareAuxState& aux_state,
                                     int64_t prearm_started_us) {
    if (telemetry.freshness != TelemetryFreshness::Fresh ||
        telemetry.data.lastValidUs <= prearm_started_us)
        return false;

    // A legacy response proves that the FC received a pre-arm control packet,
    // but its telemetry format cannot echo mode, profile, or armed state.
    if (telemetry.data.protocol == TelemetryProtocol::Original)
        return true;

    return telemetry.data.lastFlightPageUs > prearm_started_us && !telemetry.data.armed &&
           telemetry.data.flightMode == selected_flight_mode(aux_state) &&
           telemetry.data.pidProfile == aux_state.pidProfile;
}
