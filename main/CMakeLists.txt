idf_component_register(
    SRCS
        "app_main.cpp"
        "gamepad/gamepad_manager.cpp"
        "radio/nrf24.cpp"
        "radio/xn297.cpp"
        "radio/bayang.cpp"
        "telemetry/telemetry.cpp"
        "safety/failsafe.cpp"
        "storage/settings.cpp"
        "console/console.cpp"
    INCLUDE_DIRS "."
    REQUIRES "arduino" "bluepad32" "bluepad32_arduino" "btstack" "nvs_flash" "esp_netif" "esp_timer"
)
