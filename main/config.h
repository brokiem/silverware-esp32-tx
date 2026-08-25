#pragma once
#include <stdint.h>

// SPI pins
#define PIN_NRF_SCK 18
#define PIN_NRF_MOSI 23
#define PIN_NRF_MISO 19
#define PIN_NRF_CSN 5
#define PIN_NRF_CE 17
// IRQ is optional and not used in this implementation (polling is used)
#define PIN_NRF_IRQ 27

// Protocol configuration
#define BAYANG_ENABLE_TELEMETRY 1
#define BAYANG_ENABLE_ANALOG_AUX 0
#define BAYANG_RF_POWER 3  // 0=MIN, 1=LOW, 2=HIGH, 3=MAX (for NRF24)

enum class ThrottleSource : uint8_t {
    Trigger = 1,
    LeftStickHalf = 2,
};

// Select how throttle is controlled.
inline constexpr ThrottleSource THROTTLE_SOURCE = ThrottleSource::LeftStickHalf;
static_assert(THROTTLE_SOURCE == ThrottleSource::Trigger || THROTTLE_SOURCE == ThrottleSource::LeftStickHalf,
              "Unsupported throttle source");

#define ROLL_REVERSED false
#define PITCH_REVERSED false
#define YAW_REVERSED false

// Input tuning
#define STICK_DEADBAND 0.05f
#define ROLL_EXPO 0.0f
#define PITCH_EXPO 0.0f
#define YAW_EXPO 0.0f

// Safety timeouts
#define GAMEPAD_TIMEOUT_MS 500
#define TELEMETRY_TIMEOUT_MS 500
#define RADIO_TX_TIMEOUT_US 1000
#define RADIO_FAILURE_LIMIT 3
#define CONTROL_LOOP_PERIOD_MS 5
#define ARM_THROTTLE_MAX 10
#define BIND_DURATION_US 2000000
#define TELEMETRY_RX_WINDOW_US 2500

// Debug configuration
#define STATUS_PRINT_HZ 5
