#pragma once
#include <stdint.h>

// SPI pins
#define PIN_NRF_SCK 18
#define PIN_NRF_MOSI 23
#define PIN_NRF_MISO 19
#define PIN_NRF_CSN 5
#define PIN_NRF_CE 17
#define PIN_NRF_IRQ 26

// Local transmitter feedback. D27 on ESP32 DevKit boards is GPIO 27.
#define PIN_BUZZER 27
#define PIN_ONBOARD_LED 2
#define BUZZER_TONE_HZ 2400
#define ONBOARD_LED_ACTIVE_HIGH true

// Matches the FC configuration:
//   #define RX_BAYANG_PROTOCOL_TELEMETRY_AUTOBIND
//   #define RX_BAYANG_EXTENDED_TELEMETRY  // Optional; detected automatically by this TX.
//   #define USE_MULTI
#define BAYANG_ENABLE_TELEMETRY 1
#define BAYANG_ENABLE_ANALOG_AUX 0
#define BAYANG_RF_POWER 3  // 0=MIN, 1=LOW, 2=HIGH, 3=MAX (for NRF24)

// USB serial output. Text is the default; select PC telemetry in a build flag.
#define SERIAL_OUTPUT_TEXT 0
#define SERIAL_OUTPUT_PC_TELEMETRY 1
#ifndef SERIAL_OUTPUT_MODE
#define SERIAL_OUTPUT_MODE SERIAL_OUTPUT_TEXT
#endif
#if SERIAL_OUTPUT_MODE != SERIAL_OUTPUT_TEXT && SERIAL_OUTPUT_MODE != SERIAL_OUTPUT_PC_TELEMETRY
#error "Unsupported serial output mode"
#endif

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
#define THROTTLE_MAX_PERCENT 75

static_assert(THROTTLE_MAX_PERCENT >= 0 && THROTTLE_MAX_PERCENT <= 100,
              "Throttle maximum must be between 0 and 100 percent");

// Safety timeouts
#define GAMEPAD_TIMEOUT_MS 500
#define TELEMETRY_TIMEOUT_MS 500
#define RADIO_TX_TIMEOUT_US 1000
#define RADIO_FAILURE_LIMIT 3
#define CONTROL_LOOP_PERIOD_MS 5
#define ARM_THROTTLE_MAX 10
#define BIND_DURATION_US 2000000
#define PREARM_MODE_TIMEOUT_US 2000000
#define TELEMETRY_RX_DEADLINE_US 4400

static_assert(CONTROL_LOOP_PERIOD_MS == 5, "NFE Silverware telemetry expects a 5 ms packet period");
static_assert(TELEMETRY_RX_DEADLINE_US < CONTROL_LOOP_PERIOD_MS * 1000,
              "Telemetry RX deadline must fit inside the 5 ms control frame");

// Debug configuration
#define STATUS_PRINT_HZ 5
