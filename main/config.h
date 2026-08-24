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
#define BAYANG_ENABLE_TELEMETRY true
#define BAYANG_ENABLE_ANALOG_AUX false
#define BAYANG_RF_POWER 3 // 0=MIN, 1=LOW, 2=HIGH, 3=MAX (for NRF24)

// Set this to choose how throttle is controlled:
#define THROTTLE_SOURCE 2 // 1=Right Trigger, 2=Left Stick

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

// Debug configuration
#define STATUS_PRINT_HZ 5
