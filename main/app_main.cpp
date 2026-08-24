#include <Arduino.h>
#include <nvs_flash.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// BTstack related
#include <btstack_port_esp32.h>
#include <btstack_run_loop.h>
#include <btstack_stdio_esp32.h>

// Bluepad32 related
#include <arduino_platform.h>
#include <uni.h>

#include "config.h"
#include "storage/settings.h"
#include "gamepad/gamepad_manager.h"
#include "safety/failsafe.h"
#include "radio/xn297.h"
#include "radio/bayang.h"
#include "telemetry/telemetry.h"
#include "console/console.h"
#include "util/log.h"

static uint8_t tx_id[5];
static uint8_t hopping_channels[BAYANG_RF_CHANNELS];
static uint8_t current_channel_idx = 0;

void setup() {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    console_init();
    LOG("Silverware TX Starting...");

    // Load or generate TX ID
    if (!load_transmitter_id(tx_id)) {
        generate_and_save_transmitter_id(tx_id);
    }
    LOG("TX ID: %02X:%02X:%02X:%02X:%02X", tx_id[0], tx_id[1], tx_id[2], tx_id[3], tx_id[4]);

    gamepad_init();
    failsafe_init();
    telemetry_init();

    xn297_init();
    bayang_init(tx_id);
    bayang_get_hopping_channels(hopping_channels);
}

void loop() {
    int64_t loop_start = esp_timer_get_time();
    
    // Process Background Tasks
    gamepad_update();
    console_update();
    
    struct ControlState cstate;
    gamepad_get_state(&cstate);
    
    // Detect single button clicks (rising edge) so holding never re-triggers actions
    static bool prev_btnStart = false;
    static bool prev_btnMenu = false;
    static bool prev_btnB = false;

    bool start_clicked = (cstate.btnStart && !prev_btnStart);
    bool view_clicked = (cstate.btnMenu && !prev_btnMenu);
    bool b_clicked = (cstate.btnB && !prev_btnB);

    prev_btnStart = cstate.btnStart;
    prev_btnMenu = cstate.btnMenu;
    prev_btnB = cstate.btnB;

    // Intuitive single-button controls for Xbox controller:
    // START (☰) = Arm/Unlock (when LOCKED & throttle idle)
    // VIEW (⧉)  = Bind drone (when LOCKED & throttle idle)
    // START or B = Disarm/Lock (when ACTIVE)
    bool throttle_idle = (cstate.throttleRaw < 10);
    bool unlock_clicked = start_clicked;
    bool bind_clicked = view_clicked;
    bool disarm_clicked = start_clicked || b_clicked;
    
    failsafe_update(cstate.connected, cstate.lastUpdateUs, throttle_idle, unlock_clicked, bind_clicked, disarm_clicked);
    
    enum SystemState sys_state = failsafe_get_state();
    
    uint8_t packet[BAYANG_PACKET_SIZE];
    bool transmit = false;
    
    if (sys_state == STATE_BINDING) {
        bayang_build_bind_packet(packet, BAYANG_BIND_A3);
        xn297_set_tx_address((const uint8_t*)"\x00\x00\x00\x00\x00");
        xn297_set_channel(0); // Bind channel
        transmit = true;
    } else if (sys_state == STATE_ACTIVE) {
        struct BayangControlState bstate;
        bstate.roll = gamepad_get_bayang_channel(cstate.rollRaw, false, ROLL_REVERSED, STICK_DEADBAND, ROLL_EXPO);
        bstate.pitch = gamepad_get_bayang_channel(cstate.pitchRaw, false, PITCH_REVERSED, STICK_DEADBAND, PITCH_EXPO);
        bstate.yaw = gamepad_get_bayang_channel(cstate.yawRaw, false, YAW_REVERSED, STICK_DEADBAND, YAW_EXPO);
        bstate.throttle = gamepad_get_bayang_channel(cstate.throttleRaw, true, false, 0.0f, 0.0f);
        
        bstate.aux_flip = cstate.btnA;
        bstate.aux_rth = false;
        bstate.aux_headless = false;
        bstate.aux_picture = false;
        bstate.aux_video = false;
        bstate.aux_take_off = false;
        bstate.aux_inverted = false;
        bstate.aux_emg_stop = cstate.btnX; // Example mapped to X
        
        bayang_build_data_packet(packet, &bstate);
        xn297_set_tx_address(tx_id);
        xn297_set_channel(hopping_channels[current_channel_idx]);
        transmit = true;
        
        current_channel_idx = (current_channel_idx + 1) % BAYANG_RF_CHANNELS;
    } else if (sys_state == STATE_LOCKED || sys_state == STATE_WAIT_GAMEPAD) {
        // Send zero throttle packet to keep drone connected but disarmed?
        // Or send nothing? Sending nothing might trigger drone failsafe, which is good.
        // But if drone is already connected, it will beep.
        // Let's send failsafe packets (throttle=0, roll=pitch=yaw=512)
        struct BayangControlState bstate;
        bstate.roll = 512;
        bstate.pitch = 512;
        bstate.yaw = 512;
        bstate.throttle = 0;
        bstate.aux_flip = false; bstate.aux_rth = false; bstate.aux_headless = false;
        bstate.aux_picture = false; bstate.aux_video = false; bstate.aux_take_off = false;
        bstate.aux_inverted = false; bstate.aux_emg_stop = false;
        
        bayang_build_data_packet(packet, &bstate);
        xn297_set_tx_address(tx_id);
        xn297_set_channel(hopping_channels[current_channel_idx]);
        transmit = true;
        
        current_channel_idx = (current_channel_idx + 1) % BAYANG_RF_CHANNELS;
    }
    
    if (transmit) {
        xn297_set_tx_mode();
        xn297_write_payload(packet, BAYANG_PACKET_SIZE);
        
        // Wait for TX to finish (takes ~200us at 1Mbps for 22 bytes)
        int64_t tx_wait_start = esp_timer_get_time();
        while (!xn297_is_tx_done() && (esp_timer_get_time() - tx_wait_start < 1000)) {
            delayMicroseconds(10);
        }
        
#if BAYANG_ENABLE_TELEMETRY
        if (sys_state != STATE_BINDING) {
            // Switch to RX for telemetry
            xn297_set_rx_address(tx_id, BAYANG_PACKET_SIZE);
            xn297_set_rx_mode();
            
            // Wait for telemetry for up to 2.5ms
            int64_t rx_wait_start = esp_timer_get_time();
            while ((esp_timer_get_time() - rx_wait_start < 2500)) {
                if (xn297_is_rx_ready()) {
                    uint8_t rx_packet[32];
                    if (xn297_read_payload(rx_packet, BAYANG_PACKET_SIZE)) {
                        telemetry_parse(rx_packet, esp_timer_get_time());
                    }
                    break;
                }
                delayMicroseconds(20);
            }
        }
#endif
    }
    
    // Deterministic delay for 5ms loop with FreeRTOS yield to feed watchdog
    int64_t loop_end = esp_timer_get_time();
    int64_t elapsed = loop_end - loop_start;
    int64_t target_delay = 5000 - elapsed;
    
    if (target_delay >= 1000) {
        // Yield to FreeRTOS (IDLE task) for the millisecond portion
        vTaskDelay(pdMS_TO_TICKS(target_delay / 1000));
        // Precise remainder in microseconds
        int64_t remaining = 5000 - (esp_timer_get_time() - loop_start);
        if (remaining > 0) {
            delayMicroseconds(remaining);
        }
    } else {
        // Always yield at least 1 tick to prevent IDLE task starvation
        vTaskDelay(1);
    }
}

extern "C" int app_main(void) {
    btstack_init();
    uni_platform_set_custom(get_arduino_platform());
    uni_init(0, NULL);
    btstack_run_loop_execute();
    return 0;
}

