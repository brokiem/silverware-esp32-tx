# Hardware and setup

[Project overview](../README.md)

## Required hardware

- ESP32-WROOM-32 or ESP32 DevKit V1
- NRF24L01+ radio
- Xbox-compatible gamepad supported by Bluepad32
- Silverware flight controller with a Bayang receiver
- Three-pin passive buzzer module

## NRF24L01+ wiring

The transmitter uses the ESP32 VSPI bus.

| NRF24L01+ signal | ESP32 pin |
| --- | --- |
| MOSI | GPIO 23 |
| MISO | GPIO 19 |
| SCK | GPIO 18 |
| CSN | GPIO 5 |
| CE | GPIO 17 |
| IRQ | GPIO 26 |

## Local feedback wiring

Connect the three-pin buzzer module as follows:

| Buzzer pin | Connection |
| --- | --- |
| Signal, control, or `S` | GPIO 27 (`D27`) |
| Power, `VCC`, or `+` | The module's rated supply |
| Ground, `GND`, or `-` | ESP32 ground |

GPIO 27 only drives the control input. Do not use it to power the buzzer. The firmware generates a 2.4 kHz PWM tone for the passive buzzer; the frequency can be changed with `BUZZER_TONE_HZ` in `main/config.h`.

The firmware also uses the ESP32 DevKit onboard LED on GPIO 2. Both pin numbers and active levels can be changed in `main/config.h` for a different board.

| TX state | Onboard LED | Buzzer |
| --- | --- | --- |
| Waiting for gamepad | Brief pulse once per second | One short transition chirp |
| Locked | Slow blink | Lock/connect chirp |
| Binding | Fast blink | Two chirps when binding starts; three when it ends |
| Active | Solid | One arm tone |
| Gamepad failsafe | Fast blink | Repeating triple alarm |
| Radio error | Repeating double flash | Repeating double alarm |

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
