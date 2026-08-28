#include <Arduino.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#include <arduino_platform.h>
#include <btstack_port_esp32.h>
#include <btstack_run_loop.h>
#include <btstack_stdio_esp32.h>
#include <uni.h>

#include "config.h"
#include "console/console.h"
#include "feedback/feedback.h"
#include "gamepad/button_edges.h"
#include "gamepad/gamepad_manager.h"
#include "radio/bayang.h"
#include "radio/nfe_silverware_profile.h"
#include "radio/xn297.h"
#include "safety/failsafe.h"
#include "storage/settings.h"
#include "telemetry/telemetry.h"
#include "telemetry/pc_telemetry_export.h"
#include "telemetry/pc_telemetry_state.h"
#include "util/log.h"

namespace {

uint8_t tx_id[5] = {};
uint8_t hopping_channels[BAYANG_RF_CHANNELS] = {};
bool radio_initialized = false;

void publish_state_change(SystemState previous, SystemState current) {
    if (previous == current)
        return;
    feedback_notify_state(previous, current);
    console_publish_event({ConsoleEventType::StateChanged, previous, current});
}

BayangControlState neutral_control() {
    // Non-active states keep the RF link alive without allowing motor output.
    BayangControlState state = {};
    state.roll = 512;
    state.pitch = 512;
    state.yaw = 512;
    state.throttle = 0;
    return state;
}

BayangControlState locked_control(const ControlState& controls) {
    // L3 deliberately enables pitch-only FC gestures while throttle and CH5 remain off.
    const uint16_t pitch =
        gamepad_get_bayang_channel(controls.pitchRaw, false, PITCH_REVERSED, STICK_DEADBAND, PITCH_EXPO);
    return nfe_silverware_make_locked_control(controls.connected && controls.btnL3, pitch);
}

BayangControlState active_control(const ControlState& controls, const NfeSilverwareAuxState& aux_state) {
    BayangControlState state = {};
    state.roll = gamepad_get_bayang_channel(controls.rollRaw, false, ROLL_REVERSED, STICK_DEADBAND, ROLL_EXPO);
    state.pitch = gamepad_get_bayang_channel(controls.pitchRaw, false, PITCH_REVERSED, STICK_DEADBAND, PITCH_EXPO);
    state.yaw = gamepad_get_bayang_channel(controls.yawRaw, false, YAW_REVERSED, STICK_DEADBAND, YAW_EXPO);
    state.throttle = gamepad_get_bayang_channel(controls.throttleRaw, true, false, 0.0f, 0.0f);
    nfe_silverware_apply_multi_aux(&state, true, aux_state);
    return state;
}

bool transmit_packet(const uint8_t* packet) {
    xn297_set_tx_mode();
    if (!xn297_write_payload(packet, BAYANG_PACKET_SIZE))
        return false;

    const int64_t start_us = esp_timer_get_time();
    while ((esp_timer_get_time() - start_us) <= RADIO_TX_TIMEOUT_US) {
        const Xn297TxStatus status = xn297_get_tx_status();
        if (status == Xn297TxStatus::Sent)
            return true;
        if (status == Xn297TxStatus::Error)
            return false;
        delayMicroseconds(10);
    }
    return false;
}

void control_radio_task(void*) {
    // This task exclusively owns controller snapshots, safety state, RF, and telemetry.
    // Bluetooth callbacks only publish the controller pointer atomically.
    TickType_t wake_tick = xTaskGetTickCount();
    uint8_t channel_index = 0;
    uint8_t consecutive_tx_failures = 0;
    RadioStats stats = {};
    ControlState controls = {};
    bool previous_connected = false;
    ButtonEdgeState previous_buttons = {};
    NfeSilverwareAuxState aux_state = {};
    SystemState previous_state = failsafe_get_state();
    int64_t next_status_us = 0;
#if SERIAL_OUTPUT_MODE == SERIAL_OUTPUT_PC_TELEMETRY
    int64_t next_local_state_us = 0;
#endif

    if (!radio_initialized) {
        failsafe_report_radio_error();
        console_publish_event({ConsoleEventType::RadioInitFailed, previous_state, STATE_RADIO_ERROR});
        publish_state_change(previous_state, STATE_RADIO_ERROR);
        previous_state = STATE_RADIO_ERROR;
    }
#if BAYANG_ENABLE_TELEMETRY
    else if (!xn297_set_rx_address(tx_id, BAYANG_PACKET_SIZE)) {
        failsafe_report_radio_error();
        console_publish_event({ConsoleEventType::RadioInitFailed, previous_state, STATE_RADIO_ERROR});
        publish_state_change(previous_state, STATE_RADIO_ERROR);
        previous_state = STATE_RADIO_ERROR;
    }
#endif

    for (;;) {
        const int64_t cycle_start_us = esp_timer_get_time();

        gamepad_update();
        gamepad_get_state(&controls);
        if (controls.connected != previous_connected) {
            console_publish_event(
                {controls.connected ? ConsoleEventType::GamepadConnected : ConsoleEventType::GamepadDisconnected,
                 previous_state, previous_state});
            previous_connected = controls.connected;
        }

        const ButtonEdges edges =
            detect_button_edges(previous_buttons, controls.btnStart, controls.btnView, controls.btnB);

        const SystemState before_update = failsafe_get_state();
        failsafe_update_at(cycle_start_us, controls.connected, controls.lastUpdateUs,
                           controls.throttleRaw <= ARM_THROTTLE_MAX, edges.startClicked, edges.viewClicked,
                           edges.bClicked || (edges.startClicked && before_update == STATE_ACTIVE));
        SystemState state = failsafe_get_state();
        publish_state_change(previous_state, state);
        if (previous_state == STATE_BINDING && state != STATE_BINDING)
            channel_index = 0;
        previous_state = state;
        nfe_silverware_update_aux(&aux_state, state == STATE_ACTIVE, controls.btnA, controls.btnX, controls.btnY,
                                  controls.btnRB, controls.btnLB);

        uint8_t packet[BAYANG_PACKET_SIZE] = {};
        bool should_transmit = false;
        bool binding_packet = false;

        if (state == STATE_BINDING) {
            const uint8_t bind_header = bayang_select_bind_header(BAYANG_ENABLE_TELEMETRY, BAYANG_ENABLE_ANALOG_AUX);
            bayang_build_bind_packet(packet, bind_header);
            static const uint8_t bind_address[5] = {};
            xn297_set_tx_address(bind_address);
            xn297_set_channel(0);
            should_transmit = true;
            binding_packet = true;
        } else if (state != STATE_RADIO_ERROR) {
            const BayangControlState bayang =
                state == STATE_ACTIVE ? active_control(controls, aux_state)
                                      : (state == STATE_LOCKED ? locked_control(controls) : neutral_control());
            bayang_build_data_packet(packet, &bayang);
            xn297_set_tx_address(tx_id);
            xn297_set_channel(hopping_channels[channel_index]);
            channel_index = (channel_index + 1) % BAYANG_RF_CHANNELS;
            should_transmit = true;
        }

        if (should_transmit) {
            ++stats.txPackets;
            if (transmit_packet(packet)) {
                consecutive_tx_failures = 0;

#if BAYANG_ENABLE_TELEMETRY
                if (!binding_packet) {
                    // Silverware answers on the same hopping channel as the
                    // control packet. Switch to RX immediately after TX_DS and
                    // keep listening until late in the 5 ms frame, matching the
                    // timing used by established Bayang telemetry transmitters.
                    xn297_set_rx_mode();
                    const int64_t receive_deadline_us = cycle_start_us + TELEMETRY_RX_DEADLINE_US;

                    while (esp_timer_get_time() < receive_deadline_us) {
                        // IRQ goes low for RX_DR. Checking the pin avoids
                        // hammering STATUS over SPI throughout every RX window.
                        if (xn297_irq_asserted() && xn297_is_rx_ready()) {
                            uint8_t received[BAYANG_PACKET_SIZE] = {};
                            const int64_t received_us = esp_timer_get_time();
                            if (xn297_read_payload(received, sizeof(received)) && telemetry_parse(received, received_us)) {
                                ++stats.telemetryAccepted;
#if SERIAL_OUTPUT_MODE == SERIAL_OUTPUT_PC_TELEMETRY
                                pc_telemetry_export_publish_bayang(received, received_us);
#endif
                            } else {
                                ++stats.telemetryRejected;
                            }
                            break;
                        }
                        delayMicroseconds(10);
                    }
                }
#endif
            } else {
                ++stats.txFailures;
                ++consecutive_tx_failures;
                if (consecutive_tx_failures >= RADIO_FAILURE_LIMIT) {
                    // RADIO_ERROR is intentionally one-way; only a reboot retries hardware setup.
                    const SystemState old_state = failsafe_get_state();
                    failsafe_report_radio_error();
                    state = failsafe_get_state();
                    console_publish_event({ConsoleEventType::RadioRuntimeFailed, old_state, state});
                    publish_state_change(old_state, state);
                    previous_state = state;
                }
            }
        }

        const int64_t cycle_end_us = esp_timer_get_time();
        if ((cycle_end_us - cycle_start_us) > static_cast<int64_t>(CONTROL_LOOP_PERIOD_MS) * 1000) {
            ++stats.deadlineMisses;
        }

#if SERIAL_OUTPUT_MODE == SERIAL_OUTPUT_PC_TELEMETRY
        if (pc_telemetry_local_state_due(cycle_end_us, &next_local_state_us)) {
            PcTelemetryLocalStateInput local_input = {};
            local_input.nowUs = cycle_end_us;
            local_input.systemState = state;
            local_input.controls = controls;
            local_input.auxState = aux_state;
            local_input.radioInitialized = radio_initialized;
            local_input.consecutiveTxFailures = consecutive_tx_failures;
            local_input.nextHoppingChannelIndex = channel_index;
            local_input.telemetry = telemetry_get_snapshot(cycle_end_us);
            local_input.txPackets = stats.txPackets;
            local_input.txFailures = stats.txFailures;
            local_input.telemetryAccepted = stats.telemetryAccepted;
            local_input.telemetryRejected = stats.telemetryRejected;
            local_input.deadlineMisses = stats.deadlineMisses;
            local_input.exportQueueDrops = pc_telemetry_export_drop_count();
            pc_telemetry_export_publish_local_state(pc_telemetry_make_local_state(local_input), cycle_end_us);
        }
#endif

        if (cycle_end_us >= next_status_us) {
            console_publish_status({cycle_end_us, state, controls, telemetry_get_snapshot(cycle_end_us), stats});
            next_status_us = cycle_end_us + 1000000LL / STATUS_PRINT_HZ;
        }

        vTaskDelayUntil(&wake_tick, pdMS_TO_TICKS(CONTROL_LOOP_PERIOD_MS));
    }
}

}  // namespace

void setup() {
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(result);

#if SERIAL_OUTPUT_MODE == SERIAL_OUTPUT_TEXT
    if (!console_init())
        LOG("WARNING: console task unavailable");
#else
    pc_telemetry_export_init();
#endif
    LOG("Silverware TX starting");

    if (!load_transmitter_id(tx_id)) {
        if (!generate_and_save_transmitter_id(tx_id)) {
            LOG("WARNING: transmitter ID is ephemeral because persistence failed");
        }
    }
    LOG("TX ID: %02X:%02X:%02X:%02X:%02X", tx_id[0], tx_id[1], tx_id[2], tx_id[3], tx_id[4]);

    gamepad_init();
    failsafe_init();
    if (!feedback_init(failsafe_get_state()))
        LOG("WARNING: feedback task unavailable");
    telemetry_init();
    radio_initialized = xn297_init();
    bayang_init(tx_id);
    bayang_get_hopping_channels(hopping_channels);

    if (xTaskCreatePinnedToCore(control_radio_task, "control_rf", 6144, nullptr, 5, nullptr, 1) != pdPASS) {
        LOG("FATAL: could not create control/RF task");
    }
}

void loop() {
    vTaskDelay(portMAX_DELAY);
}

extern "C" int app_main(void) {
    btstack_init();
    uni_platform_set_custom(get_arduino_platform());
    uni_init(0, nullptr);
    btstack_run_loop_execute();
    return 0;
}
