#include "console.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace {

QueueHandle_t status_queue = nullptr;
QueueHandle_t event_queue = nullptr;

const char* state_name(SystemState state) {
    switch (state) {
        case STATE_BOOT:
            return "BOOT";
        case STATE_WAIT_GAMEPAD:
            return "WAIT_GAMEPAD";
        case STATE_BINDING:
            return "BINDING";
        case STATE_LOCKED:
            return "LOCKED";
        case STATE_ACTIVE:
            return "ACTIVE";
        case STATE_GAMEPAD_FAILSAFE:
            return "GAMEPAD_FAILSAFE";
        case STATE_RADIO_ERROR:
            return "RADIO_ERROR";
        default:
            return "UNKNOWN";
    }
}

void print_event(const ConsoleEvent& event) {
    switch (event.type) {
        case ConsoleEventType::StateChanged:
            Serial.printf("STATE: %s -> %s\n", state_name(event.previousState), state_name(event.currentState));
            break;
        case ConsoleEventType::GamepadConnected:
            Serial.println("GAMEPAD: connected");
            break;
        case ConsoleEventType::GamepadDisconnected:
            Serial.println("GAMEPAD: disconnected");
            break;
        case ConsoleEventType::RadioInitFailed:
            Serial.println("RADIO ERROR: initialization/readback failed; reboot required");
            break;
        case ConsoleEventType::RadioRuntimeFailed:
            Serial.println("RADIO ERROR: three consecutive transmissions failed; reboot required");
            break;
    }
}

void print_status(const ConsoleStatus& status) {
    const unsigned long long total_ms = static_cast<unsigned long long>(status.timestampUs / 1000);
    const unsigned long long hours = total_ms / 3600000ULL;
    const unsigned long long minutes = (total_ms / 60000ULL) % 60ULL;
    const unsigned long long seconds = (total_ms / 1000ULL) % 60ULL;
    const unsigned long long milliseconds = total_ms % 1000ULL;

    Serial.printf("[%02llu:%02llu:%02llu.%03llu] [STATE: %s] | Gamepad: %s | T: %u, R: %d, P: %d, Y: %d", hours,
                  minutes, seconds, milliseconds, state_name(status.state), status.controls.connected ? "CON" : "DIS",
                  static_cast<unsigned>(status.controls.throttleRaw), status.controls.rollRaw, status.controls.pitchRaw,
                  status.controls.yawRaw);

    if (status.telemetry.freshness == TelemetryFreshness::Never) {
        Serial.print(" | TELEM: never");
    } else {
        const unsigned long long age_ms = static_cast<unsigned long long>(status.telemetry.ageUs / 1000);
        Serial.printf(" | TELEM%s (%llu ms): %.2fV %u pkts/s",
                      status.telemetry.freshness == TelemetryFreshness::Stale ? " STALE" : "", age_ms,
                      status.telemetry.data.batteryCompensatedV,
                      static_cast<unsigned>(status.telemetry.data.receiverPacketsPerSecond));
    }

    Serial.printf(" | RF: tx=%lu err=%lu telem=%lu/%lu miss=%lu\n", static_cast<unsigned long>(status.radio.txPackets),
                  static_cast<unsigned long>(status.radio.txFailures),
                  static_cast<unsigned long>(status.radio.telemetryAccepted),
                  static_cast<unsigned long>(status.radio.telemetryRejected),
                  static_cast<unsigned long>(status.radio.deadlineMisses));
}

void console_task(void*) {
    ConsoleStatus status = {};
    ConsoleEvent event = {};

    for (;;) {
        while (xQueueReceive(event_queue, &event, 0) == pdTRUE)
            print_event(event);
        if (xQueueReceive(status_queue, &status, pdMS_TO_TICKS(100)) == pdTRUE)
            print_status(status);
    }
}

}  // namespace

bool console_init() {
    Serial.begin(115200);
    status_queue = xQueueCreate(1, sizeof(ConsoleStatus));
    event_queue = xQueueCreate(8, sizeof(ConsoleEvent));
    if (status_queue == nullptr || event_queue == nullptr) {
        if (status_queue != nullptr)
            vQueueDelete(status_queue);
        if (event_queue != nullptr)
            vQueueDelete(event_queue);
        status_queue = nullptr;
        event_queue = nullptr;
        return false;
    }

    if (xTaskCreatePinnedToCore(console_task, "console", 4096, nullptr, 1, nullptr, 1) != pdPASS) {
        vQueueDelete(status_queue);
        vQueueDelete(event_queue);
        status_queue = nullptr;
        event_queue = nullptr;
        return false;
    }
    return true;
}

void console_publish_status(const ConsoleStatus& status) {
    // Keep only the newest sample so console output cannot backpressure the RF task.
    if (status_queue != nullptr)
        xQueueOverwrite(status_queue, &status);
}

void console_publish_event(const ConsoleEvent& event) {
    // Drop diagnostics when full rather than delaying the RF cycle.
    if (event_queue != nullptr)
        xQueueSend(event_queue, &event, 0);
}
