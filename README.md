# Silverware TX

Silverware TX is ESP32 transmitter firmware for Silverware flight controllers. It uses an Xbox gamepad and an NRF24L01+ radio.

The transmitter sends Bayang control packets every 5 ms. It also receives and checks Silverware telemetry.

> [!WARNING]
> Remove all propellers before you bind, configure, or test the flight controller.

## Compatible flight controller

This transmitter matches the [`rajawali` branch of my NFE_Silverware fork](https://github.com/brokiem/NFE_Silverware/tree/rajawali). The flight controller must use these options:

```c
#define RX_BAYANG_PROTOCOL_TELEMETRY_AUTOBIND
#define USE_MULTI
```

The current transmitter configuration enables telemetry and disables analog AUX channels.

## Documentation

| Document | Contents |
| --- | --- |
| [Hardware and setup](docs/setup.md) | Parts, wiring, power, and the first startup |
| [Binding](docs/binding.md) | New binding, saved binding, and the FC save gesture |
| [Gamepad controls](docs/controls.md) | Sticks, buttons, channels, and flight modes |
| [How the transmitter works](docs/how-it-works.md) | TX states, task design, failsafe, radio, and telemetry |
| [Extended telemetry](docs/extended-telemetry.md) | Rotating OSD fields, packet layout, and FC configuration |
| [PC telemetry bridge](docs/pc-telemetry-bridge.md) | Binary USB framing for FC telemetry and ESP32 transmitter state |
| [Build and test](docs/development.md) | Dependencies, native tests, and the ESP32 build |

## Quick start

1. Remove all propellers.
2. Connect the radio as shown in [Hardware and setup](docs/setup.md).
3. [Build and flash the firmware](docs/development.md).
4. Follow [Binding](docs/binding.md) if the FC does not have a saved binding.
5. Review [Gamepad controls](docs/controls.md) before you arm the FC.
