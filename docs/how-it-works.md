# How the transmitter works

[Project overview](../README.md)

## TX states

| State | TX behavior |
| --- | --- |
| `WAIT_GAMEPAD` | Send Acro, centered controls, zero throttle, and all AUX channels off |
| `LOCKED` | Send Acro, centered controls, zero throttle, and all AUX channels off |
| `BINDING` | Send bind packets for two seconds |
| `PREARM_MODE` | Send the saved mode/profile with centered controls, zero throttle, and CH5 off |
| `ACTIVE` | Send gamepad controls every 5 ms |
| `GAMEPAD_FAILSAFE` | Send Acro, centered controls, zero throttle, and all AUX channels off |
| `RADIO_ERROR` | Stop transmission until the next reboot |

A recovered gamepad always returns the TX to `LOCKED`. The user must press Start again.

A binding operation also returns the TX to `LOCKED`. Binding never arms the FC.

Pressing Start at zero throttle enters `PREARM_MODE`. The TX does not enable CH5
or pass through stick controls until a fresh extended flight telemetry page
confirms the saved flight mode and PID profile and reports that the FC is still
disarmed. The FC LED setting travels in the same confirmed control packet, but
the FC telemetry protocol does not report LED state separately. Legacy
telemetry cannot echo these fields, so a fresh response instead confirms receipt
of a pre-arm packet. Missing confirmation, moving throttle, or cancelling
returns the TX to `LOCKED`.

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
The priority-1 feedback task drives the passive buzzer with PWM and controls the onboard LED from state transitions. All cue timing is non-blocking and runs outside the RF task.

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
