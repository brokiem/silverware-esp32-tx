# Silverware TX

A highly reliable RC transmitter firmware based on ESP32, an Xbox gamepad (via Bluepad32), and an NRF24L01+ module to control Silverware/Bayang protocol drones.

## Features
- **Deterministic RF loop**: Stable 5ms execution loop without delays.
- **Hardware SPI**: Direct registry access to the NRF24 bypassing abstract libraries, for precise XN297 emulation.
- **Bi-directional Telemetry**: Receives RX battery voltage, signal strength, and PIDs from the drone.
- **Xbox Wireless Control**: Connects seamlessly with standard Xbox controllers.
- **Failsafe**: Built-in state machine prevents accidental motor spin-ups on boot and automatically stops the motors if the gamepad disconnects.

## Hardware Setup
- **ESP32-WROOM-32** (Standard DevKit V1)
- **NRF24L01+** connected to hardware VSPI:
  - MOSI: GPIO 23
  - MISO: GPIO 19
  - SCK: GPIO 18
  - CSN: GPIO 16
  - CE: GPIO 17
- Remember to power the NRF24 from a clean 3.3V source!

## Controls & Button Layout (Xbox Controller)
- **Left Stick (Push Up)**: Throttle (Center & below is 0% throttle with deadband)
- **Left Stick (Left/Right)**: Yaw
- **Right Stick**: Pitch (Up/Down) & Roll (Left/Right)
- **START Button (☰)**: **Arm / Unlock** (press with throttle at 0) or **Disarm / Lock** toggle
- **VIEW Button (⧉)**: **Bind Drone** (press with throttle at 0 while in `LOCKED` state)
- **B Button**: **Emergency Stop / Quick Disarm**
- **A Button**: AUX1 (Flip / Mode)
- **X Button**: AUX2 (Inverted / Turtle Mode)

## Development
- Unit tests: `pio test -e native`
- Build: `pio run -e esp32dev`
