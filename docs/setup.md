# Hardware and setup

[Project overview](../README.md)

## Required hardware

- ESP32-WROOM-32 or ESP32 DevKit V1
- NRF24L01+ radio
- Xbox-compatible gamepad supported by Bluepad32
- Silverware flight controller with a Bayang receiver

## NRF24L01+ wiring

The transmitter uses the ESP32 VSPI bus.

| NRF24L01+ signal | ESP32 pin |
| --- | --- |
| MOSI | GPIO 23 |
| MISO | GPIO 19 |
| SCK | GPIO 18 |
| CSN | GPIO 5 |
| CE | GPIO 17 |

The firmware does not use the NRF24 IRQ signal.

Supply the radio with clean 3.3 V power. Install a local decoupling capacitor near the radio.

Do not connect the radio to a 5 V supply.

## First startup

> [!WARNING]
> Remove all propellers before you power the flight controller.

1. Check each NRF24L01+ connection.
2. Power the ESP32 transmitter.
3. Power the flight controller.
4. Connect the gamepad to the ESP32.
5. Read the console state.

The TX loads its stored transmitter ID during startup. It then sends neutral packets with zero throttle.

If the FC has the same saved binding, it accepts these packets automatically. If not, follow the [binding procedure](binding.md).

The TX stays in `LOCKED` after it connects. Press Start at zero throttle when you are ready to arm.
