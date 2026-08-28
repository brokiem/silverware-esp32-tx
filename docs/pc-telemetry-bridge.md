# PC telemetry bridge

[Project overview](../README.md) · [Extended FC telemetry](extended-telemetry.md)

The optional PC telemetry build exports two binary message types over the ESP32's UART0 USB connection:

- Type `1`: every valid original `0x85` or extended `0x86` Bayang telemetry packet, unchanged.
- Type `2`: a complete ESP32 transmitter-state snapshot at 20 Hz.

Local-state snapshots continue when FC telemetry is absent. This lets a PC distinguish FC telemetry loss, gamepad loss, a locked safety state, and complete USB-transmitter disconnection.

## Build and flash

The normal `esp32dev` environment keeps the human-readable text console. Build the binary variant with:

```sh
pio run -e esp32dev-pc-telemetry
pio run -e esp32dev-pc-telemetry -t upload
```

Both modes use 115200 baud. `SERIAL_OUTPUT_MODE` selects exactly one mode at compile time. Application status, event text, and `LOG()` output are disabled in the binary build and cannot be mixed into its stream.

## Export behavior

The 5 ms control/radio task only copies messages into a 16-entry FreeRTOS queue. It publishes accepted FC telemetry immediately and local state every 50,000 us. If a local-state deadline is missed, the next deadline is scheduled from the actual publication time; there is no catch-up burst.

A priority-1 task on core 1 performs framing and UART output. If the queue is full, the oldest entry is discarded and the queue-drop counter increments before the newest entry is queued. The following local-state snapshot reports the updated counter.

Message types `1` and `2` have independent 16-bit sequences. Each sequence advances when its message is accepted for export, even if that entry is later discarded from a full queue. Track the two sequences independently. A type-1 gap reports an FC export loss, while a type-2 gap reports a local-state export loss. The sequence nibble inside an extended `0x86` packet separately reports telemetry responses lost between the FC and ESP32.

## Common serial framing

Each decoded record has a 14-byte envelope, a variable-length payload, and a two-byte CRC:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | Protocol version: `1` |
| 1 | 1 | Message type |
| 2 | 2 | Payload length, big-endian |
| 4 | 2 | Per-type sequence, big-endian, modulo 65,536 |
| 6 | 8 | ESP timestamp, microseconds since boot, big-endian |
| 14 | variable | Message payload |
| 14 + length | 2 | Record CRC, big-endian |

CRC uses CRC-16/CCITT-FALSE over every record byte before the CRC:

| Parameter | Value |
| --- | --- |
| Polynomial | `0x1021` |
| Initial value | `0xFFFF` |
| Input/output reflection | No |
| Final XOR | `0x0000` |

The complete record, including its CRC, is COBS encoded and followed by one `0x00` delimiter. Integers in the envelope and both payloads are big-endian.

ROM boot text can appear first. The application writes an initial `0x00`; a reader must discard all input through that delimiter before parsing frames.

## Type 1: raw FC telemetry

Type `1` has a 15-byte payload containing an unmodified Bayang `0x85` or `0x86` packet. The decoded record is 31 bytes and its COBS frame is at most 33 bytes. The raw Bayang checksum remains in payload byte 14. Validate the bridge CRC first and the Bayang checksum second.

### Type-1 golden frame

For sequence `0xFFFE`, timestamp `0x0102030405060708`, and this extended packet:

```text
86 2A 00 00 00 00 00 00 00 00 00 00 00 00 B0
```

the record CRC is `0x9E78`, and the complete COBS frame including its delimiter is:

```text
03 01 01 0E 0F FF FE 01 02 03 04 05 06 07 08 86 2A
01 01 01 01 01 01 01 01 01 01 01 01 04 B0 9E 78 00
```

## Type 2: ESP32 local state

Type `2` has this exact 44-byte payload:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | System state |
| 1 | 2 | Status flags |
| 3 | 2 | Current gamepad buttons |
| 5 | 1 | Latched FC auxiliary modes |
| 6 | 1 | Consecutive radio TX failures |
| 7 | 1 | Next hopping-channel index, `0–3` |
| 8 | 2 | Raw roll, signed |
| 10 | 2 | Raw pitch, signed |
| 12 | 2 | Raw yaw, signed |
| 14 | 2 | Raw throttle, unsigned |
| 16 | 2 | Gamepad update age in milliseconds |
| 18 | 2 | FC telemetry age in milliseconds |
| 20 | 4 | Radio TX packet total |
| 24 | 4 | Radio TX failure total |
| 28 | 4 | FC telemetry accepted total |
| 32 | 4 | FC telemetry rejected total |
| 36 | 4 | Control-loop deadline miss total |
| 40 | 4 | USB exporter queue-drop total |

Age values saturate at 65,534 ms. `0xFFFF` means that no update has ever been received. Buttons and raw controls remain present after disconnection, but consumers must check the connected and fresh flags before using them.

### System states

| Value | State |
| ---: | --- |
| 0 | Boot |
| 1 | Waiting for gamepad |
| 2 | Binding |
| 3 | Locked |
| 4 | Active |
| 5 | Gamepad failsafe |
| 6 | Radio error |

### Status flags

| Bit | Meaning |
| ---: | --- |
| 0 | Gamepad connected |
| 1 | Gamepad fresh: connected and age no greater than `GAMEPAD_TIMEOUT_MS` |
| 2 | Safety locked: system state is not active |
| 3 | Control enabled: system state is active |
| 4 | Binding |
| 5 | Gamepad failsafe |
| 6 | Radio error |
| 7 | Radio initialized |
| 8 | FC telemetry has been received |
| 9 | FC telemetry fresh |
| 10 | FC telemetry stale |
| 15–11 | Reserved, zero |

### Button and auxiliary flags

Button bits `0–13` are A, B, X, Y, LB, RB, L3, R3, Start, View, D-pad up, down, left, and right. Bits `14–15` are zero.

Auxiliary bits `0–4` are level, race, horizon, alternate PID profile, and LEDs. Bits `5–7` are zero.

### Type-2 golden frame

This vector exercises signed values, all defined flags, and all counters. It uses sequence `0xFFFF`, timestamp `0x0102030405060708`, system state `5`, status `0x07FF`, buttons `0x3FFF`, auxiliary flags `0x1F`, and representative values for the remaining fields. Its CRC is `0xEA8F`.

The complete 62-byte COBS frame, including its delimiter, is:

```text
03 01 02 19 2C FF FF 01 02 03 04 05 06 07 08 05 07 FF
3F FF 1F 03 02 FB 2E 09 29 80 03 03 FF 1E 2A FF FE 01
02 03 04 11 12 13 14 21 22 23 24 31 32 33 34 41 42
43 44 51 52 53 54 EA 8F 00
```

The decoded type-2 record is 60 bytes and its COBS frame is at most 62 bytes. At 200 type-1 frames and 20 type-2 frames per second, worst-case traffic is 7,840 bytes/second, below the approximately 11,520 bytes/second available at 115200 baud.

## PC reader behavior

The included [ground station](../tools/ground_station/README.md) implements this reader, a responsive dashboard, automatic serial reconnection, and rotating blackbox logs:

```sh
python3 -m pip install -r tools/ground_station/requirements.txt
python3 tools/ground_station/ground_station.py
```

1. Open the serial port at 115200 baud.
2. Discard input through the first `0x00` synchronization delimiter.
3. Split subsequent input at each `0x00`.
4. COBS-decode each nonempty segment. Discard malformed segments and resume at the next delimiter.
5. Validate record length, version `1`, known message type, and matching payload length.
6. Verify CRC-16/CCITT-FALSE.
7. For type `1`, verify the Bayang checksum and decode `0x85` or `0x86` with the [extended telemetry specification](extended-telemetry.md).
8. For type `2`, decode the 44-byte snapshot and refresh local transmitter state.
9. Track type-1 and type-2 sequences separately.

For extended FC telemetry, aggregate A, B, C, D0, and D1 into one latest snapshot. Update only fields belonging to the page that arrived, and retain an availability mask and timestamp for every page. Rendering should read the latest snapshot at video-frame rate and never block on serial input.

Use the PC's monotonic receive clock for health decisions:

- Mark local state stale after 250 ms without type `2`.
- Display `FC TELEMETRY LOST` after 500 ms without type `1` while type `2` continues.
- Treat the complete transmitter/USB bridge as disconnected after 500 ms without either type.
- Display safety as off only when `CONTROL_ENABLED` is set. Every other state is safety-on/locked.

The ESP timestamp is useful for ordering and logging samples, but not for the PC-side disconnect timeout. Message type `3` is reserved for possible future events; state transitions are represented by periodic type-2 snapshots.
