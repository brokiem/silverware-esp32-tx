#include "pc_telemetry_export.h"

#include "../config.h"

#include <string.h>

#if SERIAL_OUTPUT_MODE == SERIAL_OUTPUT_PC_TELEMETRY

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace {

constexpr UBaseType_t EXPORT_QUEUE_LENGTH = 16;

struct ExportItem {
    uint8_t messageType;
    uint16_t sequence;
    uint64_t timestampUs;
    uint8_t packet[BAYANG_PACKET_SIZE];
    bool overlaySavedFlightConfig;
    uint8_t savedAuxFlags;
    PcTelemetryLocalState localState;
};

QueueHandle_t export_queue = nullptr;
uint16_t next_bayang_sequence = 0;
uint16_t next_local_state_sequence = 0;
uint32_t export_queue_drops = 0;

void enqueue(const ExportItem& item) {
    if (export_queue == nullptr)
        return;
    if (xQueueSend(export_queue, &item, 0) == pdTRUE)
        return;

    ExportItem discarded = {};
    if (xQueueReceive(export_queue, &discarded, 0) == pdTRUE)
        ++export_queue_drops;
    xQueueSend(export_queue, &item, 0);
}

void export_task(void*) {
    ExportItem item = {};
    uint8_t frame[PC_TELEMETRY_MAX_FRAME_SIZE] = {};

    for (;;) {
        if (xQueueReceive(export_queue, &item, portMAX_DELAY) != pdTRUE)
            continue;

        size_t frame_length = 0;
        if (item.messageType == PC_TELEMETRY_MESSAGE_RAW_BAYANG) {
            if (item.overlaySavedFlightConfig)
                pc_telemetry_overlay_saved_flight_config(item.packet, item.savedAuxFlags);
            PcTelemetrySample sample = {};
            pc_telemetry_make_sample(&sample, item.packet, item.timestampUs, item.sequence);
            frame_length = pc_telemetry_encode_frame(sample, frame, sizeof(frame));
        } else if (item.messageType == PC_TELEMETRY_MESSAGE_LOCAL_STATE) {
            frame_length = pc_telemetry_encode_local_state_frame(item.localState, item.sequence, item.timestampUs,
                                                                 frame, sizeof(frame));
        }
        if (frame_length != 0)
            Serial.write(frame, frame_length);
    }
}

}  // namespace

bool pc_telemetry_export_init() {
    Serial.begin(115200);
    Serial.write(static_cast<uint8_t>(0));

    export_queue = xQueueCreate(EXPORT_QUEUE_LENGTH, sizeof(ExportItem));
    if (export_queue == nullptr)
        return false;
    if (xTaskCreatePinnedToCore(export_task, "pc_telem", 4096, nullptr, 1, nullptr, 1) != pdPASS) {
        vQueueDelete(export_queue);
        export_queue = nullptr;
        return false;
    }
    return true;
}

void pc_telemetry_export_publish_bayang(const uint8_t* packet, int64_t timestamp_us,
                                        bool overlay_saved_flight_config, uint8_t saved_aux_flags) {
    if (packet == nullptr)
        return;
    ExportItem item = {};
    item.messageType = PC_TELEMETRY_MESSAGE_RAW_BAYANG;
    item.sequence = next_bayang_sequence++;
    item.timestampUs = static_cast<uint64_t>(timestamp_us);
    memcpy(item.packet, packet, BAYANG_PACKET_SIZE);
    item.overlaySavedFlightConfig = overlay_saved_flight_config;
    item.savedAuxFlags = saved_aux_flags;
    enqueue(item);
}

void pc_telemetry_export_publish_local_state(const PcTelemetryLocalState& state, int64_t timestamp_us) {
    ExportItem item = {};
    item.messageType = PC_TELEMETRY_MESSAGE_LOCAL_STATE;
    item.sequence = next_local_state_sequence++;
    item.timestampUs = static_cast<uint64_t>(timestamp_us);
    item.localState = state;
    enqueue(item);
}

uint32_t pc_telemetry_export_drop_count() {
    return export_queue_drops;
}

#else

bool pc_telemetry_export_init() {
    return true;
}

void pc_telemetry_export_publish_bayang(const uint8_t*, int64_t, bool, uint8_t) {}

void pc_telemetry_export_publish_local_state(const PcTelemetryLocalState&, int64_t) {}

uint32_t pc_telemetry_export_drop_count() {
    return 0;
}

#endif
