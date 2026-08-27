idf_component_register(
    SRCS
        "app_main.cpp"
        "gamepad/gamepad_manager.cpp"
        "gamepad/control_mapping.cpp"
        "gamepad/button_edges.cpp"
        "radio/nrf24.cpp"
        "radio/xn297.cpp"
        "radio/bayang.cpp"
        "radio/nfe_silverware_profile.cpp"
        "telemetry/telemetry.cpp"
        "telemetry/pc_telemetry_protocol.cpp"
        "telemetry/pc_telemetry_state.cpp"
        "telemetry/pc_telemetry_export.cpp"
        "safety/failsafe.cpp"
        "storage/settings.cpp"
        "storage/settings_core.cpp"
        "console/console.cpp"
    INCLUDE_DIRS "."
    REQUIRES "espressif__arduino-esp32" "bluepad32" "bluepad32_arduino" "btstack" "nvs_flash" "esp_netif" "esp_timer"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
set_source_files_properties(
    "gamepad/control_mapping.cpp"
    "gamepad/button_edges.cpp"
    "radio/bayang.cpp"
    "radio/nfe_silverware_profile.cpp"
    "telemetry/telemetry.cpp"
    "telemetry/pc_telemetry_protocol.cpp"
    "telemetry/pc_telemetry_state.cpp"
    "safety/failsafe.cpp"
    "storage/settings_core.cpp"
    PROPERTIES COMPILE_OPTIONS "-Wundef"
)
