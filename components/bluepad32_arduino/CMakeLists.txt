include(CMakePrintHelpers)

set(srcs
    "ArduinoBluepad32.cpp"
    "ArduinoConsole.cpp"
    "ArduinoController.cpp"
    "ArduinoGamepad.cpp"
    "arduino_bootstrap.cpp"
    "arduino_platform.c"
)

set(requires
    espressif__arduino-esp32
    bluepad32
    btstack
    esp_netif
)

idf_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "include"
    REQUIRES ${requires}
)

# Bluepad32 references setup() and loop(). Adding main to PRIV_REQUIRES would
# create a circular dependency, so force the linker to retain those symbols.
set(ext_symbols "-u _Z6setupv -u _Z4loopv")

string(REPLACE ";" " " ext_symbols "${ext_symbols}")
target_link_libraries(${COMPONENT_LIB} INTERFACE ${ext_symbols})
