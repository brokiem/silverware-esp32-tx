#include "gamepad_manager.h"
#include <Bluepad32.h>
#include <esp_timer.h>
#include <atomic>
#include "../config.h"
#include "control_mapping.h"

static std::atomic<ControllerPtr> myController{nullptr};
static ControlState current_state = {};

static void onConnectedController(ControllerPtr ctl) {
    ControllerPtr expected = nullptr;
    myController.compare_exchange_strong(expected, ctl);
}

static void onDisconnectedController(ControllerPtr ctl) {
    ControllerPtr expected = ctl;
    myController.compare_exchange_strong(expected, nullptr);
}

void gamepad_init() {
    BP32.setup(&onConnectedController, &onDisconnectedController);
    BP32.enableVirtualDevice(false);
    current_state = {};
}

void gamepad_update() {
    const bool dataUpdated = BP32.update();
    const ControllerPtr controller = myController.load();
    if (controller == nullptr || !controller->isConnected()) {
        // Clear commands before the safety layer observes the disconnect.
        current_state = {};
        return;
    }

    if (dataUpdated) {
        current_state.rollRaw = controller->axisRX();
        current_state.pitchRaw = -controller->axisRY();  // Invert Y so up is positive
        current_state.yawRaw = controller->axisX();

        if constexpr (THROTTLE_SOURCE == ThrottleSource::Trigger) {
            current_state.throttleRaw = map_trigger_throttle(controller->throttle());
        } else if constexpr (THROTTLE_SOURCE == ThrottleSource::LeftStickHalf) {
            current_state.throttleRaw = map_half_stick_throttle(controller->axisY(), STICK_DEADBAND);
        }

        current_state.btnA = controller->a();
        current_state.btnB = controller->b();
        current_state.btnX = controller->x();
        current_state.btnY = controller->y();
        current_state.btnLB = controller->l1();
        current_state.btnRB = controller->r1();
        current_state.btnL3 = controller->thumbL();
        current_state.btnR3 = controller->thumbR();
        current_state.btnStart = controller->miscStart();
        current_state.btnView = controller->miscSelect();

        current_state.btnDPadUp = controller->dpad() & 0x01;
        current_state.btnDPadDown = controller->dpad() & 0x02;
        current_state.btnDPadRight = controller->dpad() & 0x04;
        current_state.btnDPadLeft = controller->dpad() & 0x08;

        current_state.connected = true;
        current_state.lastUpdateUs = esp_timer_get_time();
    }
}

void gamepad_get_state(ControlState* state) {
    *state = current_state;
}

uint16_t gamepad_get_bayang_channel(int32_t raw_value, bool is_throttle, bool reversed, float deadband, float expo) {
    return map_bayang_channel(raw_value, is_throttle, reversed, deadband, expo);
}
