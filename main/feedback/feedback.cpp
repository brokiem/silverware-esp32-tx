#include "feedback.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "../config.h"
#include "feedback_pattern.h"

namespace {

constexpr TickType_t FEEDBACK_UPDATE_TICKS = pdMS_TO_TICKS(10);
volatile uint32_t requested_transition = 0;

uint32_t pack_transition(SystemState previous, SystemState current) {
    return (static_cast<uint32_t>(previous) << 8) | static_cast<uint32_t>(current);
}

void write_active_level(uint8_t pin, bool on, bool active_high) {
    digitalWrite(pin, on == active_high ? HIGH : LOW);
}

void feedback_task(void*) {
    uint32_t active_transition = __atomic_load_n(&requested_transition, __ATOMIC_ACQUIRE);
    uint32_t transition_started_ms = millis();
    bool buzzer_on = false;

    for (;;) {
        const uint32_t requested = __atomic_load_n(&requested_transition, __ATOMIC_ACQUIRE);
        if (requested != active_transition) {
            active_transition = requested;
            transition_started_ms = millis();
        }

        const SystemState previous = static_cast<SystemState>((active_transition >> 8) & 0xff);
        const SystemState current = static_cast<SystemState>(active_transition & 0xff);
        const FeedbackOutput output = feedback_pattern(previous, current, millis() - transition_started_ms);
        write_active_level(PIN_ONBOARD_LED, output.ledOn, ONBOARD_LED_ACTIVE_HIGH);
        if (output.buzzerOn != buzzer_on) {
            ledcWriteTone(PIN_BUZZER, output.buzzerOn ? BUZZER_TONE_HZ : 0);
            buzzer_on = output.buzzerOn;
        }
        vTaskDelay(FEEDBACK_UPDATE_TICKS);
    }
}

}  // namespace

bool feedback_init(SystemState initial_state) {
    pinMode(PIN_ONBOARD_LED, OUTPUT);
    write_active_level(PIN_ONBOARD_LED, false, ONBOARD_LED_ACTIVE_HIGH);
    if (!ledcAttach(PIN_BUZZER, BUZZER_TONE_HZ, 10))
        return false;
    ledcWriteTone(PIN_BUZZER, 0);
    __atomic_store_n(&requested_transition, pack_transition(STATE_BOOT, initial_state), __ATOMIC_RELEASE);
    if (xTaskCreatePinnedToCore(feedback_task, "feedback", 2048, nullptr, 1, nullptr, 0) == pdPASS)
        return true;
    ledcDetach(PIN_BUZZER);
    return false;
}

void feedback_notify_state(SystemState previous, SystemState current) {
    if (previous == current)
        return;
    __atomic_store_n(&requested_transition, pack_transition(previous, current), __ATOMIC_RELEASE);
}
