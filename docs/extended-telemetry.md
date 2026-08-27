# Extended telemetry

[Project overview](../README.md)

The transmitter accepts original NFE Silverware telemetry (`0x85`) and extended telemetry V1 (`0x86`). Both formats remain 15 bytes and use the existing additive checksum in byte 14. Binding, radio timing, and the 5 ms control-packet period do not change.

## Enable it on the flight controller

The [`rajawali` branch](https://github.com/brokiem/NFE_Silverware/tree/rajawali) belongs to the [`brokiem/NFE_Silverware` fork](https://github.com/brokiem/NFE_Silverware), not the upstream repository. Its flight-controller configuration enables:

```c
#define RX_BAYANG_PROTOCOL_TELEMETRY_AUTOBIND
#define RX_BAYANG_EXTENDED_TELEMETRY
```

`RX_BAYANG_EXTENDED_TELEMETRY` requires the telemetry-autobind receiver. Removing the extended define restores the original `0x85` packet byte for byte. The transmitter detects either format automatically.

## Common packet layout

| Byte | Contents |
| --- | --- |
| 0 | `0x86` extended V1 header |
| 1 | Common status header |
| 2–13 | 96-bit page payload |
| 14 | Low eight bits of the sum of bytes 0–13 |

Payload fields are packed most-significant bit first. Unsigned multibit values use normal binary representation; signed values use two's complement.

### Common status header

| Bits | Meaning |
| --- | --- |
| 7–6 | Page: `0` A/control, `1` B/flight, `2` C/power-link, `3` D/system |
| 5 | Armed |
| 4 | Receiver failsafe |
| 3–0 | Telemetry sequence, modulo 16 |

Armed and failsafe therefore update in every valid telemetry response. The transmitter uses the sequence nibble to count missing telemetry responses.

## Schedule

The deterministic cycle is:

```text
A B A C A B A D
```

At 200 successful control/telemetry exchanges per second, this produces:

| Page | Rate | Purpose |
| --- | ---: | --- |
| A | 100 Hz | Control response and motor outputs |
| B | 50 Hz | Attitude, acceleration, and flight state |
| C | 25 Hz | Battery and receiver-link quality |
| D | 25 Hz | System health; D0 and D1 alternate at 12.5 Hz each |

Actual update rates fall with control-packet loss because the FC only responds to received packets.

## A — Control, 100 Hz

| Bit offset | Width | Field | Encoding |
| ---: | ---: | --- | --- |
| 0 | 10 | Gyro roll | signed, 4 deg/s per count |
| 10 | 10 | Gyro pitch | signed, 4 deg/s per count |
| 20 | 10 | Gyro yaw | signed, 4 deg/s per count |
| 30 | 10 | Setpoint roll | signed, 4 deg/s per count |
| 40 | 10 | Setpoint pitch | signed, 4 deg/s per count |
| 50 | 10 | Setpoint yaw | signed, 4 deg/s per count |
| 60 | 6 | Commanded throttle | `count / 63`, exposed as percent |
| 66 | 6 | Applied throttle | `count / 63`, exposed as percent |
| 72 | 6 | Motor 1 | `count / 63`, exposed as percent |
| 78 | 6 | Motor 2 | `count / 63`, exposed as percent |
| 84 | 6 | Motor 3 | `count / 63`, exposed as percent |
| 90 | 6 | Motor 4 | `count / 63`, exposed as percent |

The gyro and setpoint ranges are -2048 through +2044 deg/s. Commanded throttle is the received pilot command; applied throttle is the value after the FC's throttle processing.

## B — Flight state, 50 Hz

| Bit offset | Width | Field | Encoding |
| ---: | ---: | --- | --- |
| 0 | 12 | Roll | signed, 0.1 degree per count |
| 12 | 12 | Pitch | signed, 0.1 degree per count |
| 24 | 12 | Relative yaw | signed, 0.1 degree per count |
| 36 | 12 | Acceleration X | signed Q8 g (`1 g = 256`) |
| 48 | 12 | Acceleration Y | signed Q8 g |
| 60 | 12 | Acceleration Z | signed Q8 g |
| 72 | 16 | Current-flight time | seconds |
| 88 | 8 | Flight flags | bit field below |

Acceleration is captured after calibration and conversion to g, before the attitude estimator can normalize its working buffer. Signed Q8 covers -8.0 through approximately +7.996 g.

Relative yaw is integrated from the yaw gyro, wrapped to ±180 degrees, and reset when a flight starts. Silverware has no heading reference, so this value drifts and is not an absolute compass heading.

Flight time resets on the transition to armed and off-ground, remains available after landing, and saturates at 65,535 seconds.

### Flight flags

| Bit | Meaning |
| ---: | --- |
| 0 | On ground / motors inactive |
| 1 | Idle-up active |
| 2 | Low battery |
| 3 | Level mode active |
| 4 | Race mode active |
| 5 | Horizon mode active |
| 6 | Alternate PID profile active |
| 7 | Reserved, zero |

The transmitter derives `Acro`, `Level`, `Race`, `Horizon`, or `RaceHorizon` from bits 3–5.

## C — Power and link, 25 Hz

| Bit offset | Width | Field | Encoding |
| ---: | ---: | --- | --- |
| 0 | 16 | Raw battery voltage | millivolts |
| 16 | 16 | Compensated battery voltage | millivolts |
| 32 | 8 | RX packet rate | packets/second |
| 40 | 8 | Estimated lost packets | packets in the last one-second window |
| 48 | 8 | Link quality | percent |
| 56 | 8 | Battery/status flags | bit 0 low battery; bits 7–1 reserved |
| 64 | 16 | Maximum RX gap | 0.1 ms per count, last one-second window |
| 80 | 8 | Latest RX inter-packet gap | 0.1 ms per count |
| 88 | 8 | Failsafe-entry count | count for this power cycle |

The latest inter-packet gap is used instead of current RX age: Bayang telemetry is transmitted in response to an incoming control packet, so age at transmission would normally be near zero. The maximum gap reveals dropouts that an average RX rate can hide. Link quality is derived from the expected 200 control packets per second; no synthetic RSSI is reported.

## D — System, 25 Hz

Payload bit 95 (the first transmitted bit, shown as offset 0 below) is the D subpage discriminator. D0 and D1 alternate.

### D0 — Health, 12.5 Hz

| Bit offset | Width | Field | Encoding |
| ---: | ---: | --- | --- |
| 0 | 1 | Subpage | `0` |
| 1 | 16 | Loop-time average | microseconds |
| 17 | 16 | Loop-time maximum | microseconds |
| 33 | 16 | Loop overrun count | cumulative count for this power cycle |
| 49 | 16 | IMU temperature | signed raw sensor register |
| 65 | 8 | IMU type | raw WHO_AM_I value |
| 73 | 8 | CPU load | loop work time as percent of loop budget |
| 81 | 15 | Telemetry TX counter | low 15 bits |

The FC resets the loop average, maximum, and sample accumulators after encoding D0. The cumulative overrun counter saturates at 65,535.

### D1 — Counters, 12.5 Hz

| Bit offset | Width | Field | Encoding |
| ---: | ---: | --- | --- |
| 0 | 1 | Subpage | `1` |
| 1 | 32 | Valid RX packet total | low 32 bits |
| 33 | 32 | Estimated lost-packet total | low 32 bits |
| 65 | 31 | Telemetry TX counter | low 31 bits |

## Saturation and counters

The FC clamps every signed or scaled measurement to its encoded range; values never wrap. Voltage and loop measurements clamp to 16 bits, RX rate and link fields clamp to 8 bits, the maximum gap clamps to 16 bits, and the latest gap and failsafe count clamp to 8 bits. Explicit low-bit counters in D0/D1 wrap naturally when their transmitted width is exhausted.

## Transmitter API and compatibility

`TelemetryData::protocol` identifies `Original` or `ExtendedV1`. `extendedPagesSeen` records which of A–D have arrived, and `systemSubpagesSeen` records D0/D1 separately. Consumers should check these masks before displaying fields from a rotating page. Packet-level freshness continues to describe the most recent valid complete telemetry response.

When the source changes between original and extended telemetry, the transmitter clears all extended values and availability masks so stale fields cannot look current. Original `0x85` battery, link-rate, and rotating PID parsing remains unchanged. Extended payload bytes are never interpreted as legacy PID values.

The console status line adds protocol, armed/mode state, flight time, roll/pitch/relative-yaw, and commanded throttle. Gyro, setpoint, motor, link diagnostics, and system health remain available through `TelemetrySnapshot` for an OSD or logger.
