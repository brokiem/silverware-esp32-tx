# How the transmitter works

[Project overview](../README.md)

## TX states

| State | TX behavior |
| --- | --- |
| `WAIT_GAMEPAD` | Send centered controls with zero throttle |
| `LOCKED` | Send centered controls with zero throttle |
| `BINDING` | Send bind packets for two seconds |
| `ACTIVE` | Send gamepad controls every 5 ms |
| `GAMEPAD_FAILSAFE` | Send centered controls with zero throttle |
| `RADIO_ERROR` | Stop transmission until the next reboot |

A recovered gamepad always returns the TX to `LOCKED`. The user must press Start again.

A binding operation also returns the TX to `LOCKED`. Binding never arms the FC.

## Task design

The priority-5 control and RF task runs on ESP32 core 1. It runs once every 5 ms.

This task owns these items:

- Gamepad snapshots
- Button edge detection
- Safety state
- Bayang packet creation
- RF hopping index
- NRF24 SPI access
- Telemetry state

Bluetooth and the ESP main task run on core 0.

The priority-1 console task prints status and events. The RF task never waits for UART output.

## Radio safety

The TX checks the NRF24 registers during startup. It also waits for the radio startup period before transmission.

A radio initialization failure sets `RADIO_ERROR`. Three consecutive transmission failures also set `RADIO_ERROR`.

`RADIO_ERROR` remains active until reboot. The TX does not transmit in this state.

## Telemetry checks

The TX accepts a telemetry packet only when its header and Bayang checksum are correct. A rejected packet cannot change telemetry values.

Silverware telemetry does not include an XN297 hardware CRC. The TX therefore keeps the NRF24 hardware CRC disabled for telemetry.

Telemetry has three freshness states:

| State | Meaning |
| --- | --- |
| `Never` | The TX has not received a valid telemetry packet |
| `Fresh` | The last valid packet is less than 500 ms old |
| `Stale` | The last valid packet is at least 500 ms old |

The TX keeps the last valid value after it becomes stale. The console also shows its age for diagnosis.

The console reports checksum rejects, TX errors, deadline misses, and accepted packet rates.
