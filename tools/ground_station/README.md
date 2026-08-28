# Silverware TX ground station

The ground station displays live USB telemetry and records an independent blackbox session for every live run.

## Run

```sh
python3 -m pip install -r tools/ground_station/requirements.txt
python3 tools/ground_station/ground_station.py
```

Pass a serial port to bypass initial auto-detection:

```sh
python3 tools/ground_station/ground_station.py /dev/ttyUSB0
```

Useful recording options:

```sh
python3 tools/ground_station/ground_station.py --logs-dir /path/to/logs
python3 tools/ground_station/ground_station.py --max-log-storage 512M
python3 tools/ground_station/ground_station.py --max-log-storage 0
python3 tools/ground_station/ground_station.py --no-blackbox
```

The default quota is 2 GiB. `0` disables automatic pruning.
Typical combined raw and decoded storage is approximately 40–60 MiB per hour before workload-dependent compression savings.

Inspect the newest session in a logs directory after a flight:

```sh
python3 tools/ground_station/ground_station.py --inspect-log tools/ground_station/logs
```

Pass a specific session directory to inspect that run. The inspector validates every available raw-record CRC and reports connection IDs, warnings, telemetry rows, flight modes, minimum battery voltage, and maximum RX gap. Add `--inspect-json` for machine-readable output. A failed integrity check returns exit status 1.

## Session files

Sessions are stored in `logs/<UTC timestamp>_<process ID>/`:

- `session.json` contains session metadata, counters, connections, chunk inventory, compression results, and pruned ranges.
- `events.jsonl` contains one JSON event per line for connection changes, reboots, telemetry health, flight-state changes, warnings, rotation, and shutdown.
- `telemetry-NNNNNN.csv.gz` contains decoded aggregate snapshots at 10 Hz while armed/control-enabled and 2 Hz while inactive.
- `serial-NNNNNN.sbb.gz` contains the lossless serial byte stream, including boot output and malformed data.

Raw and CSV chunks rotate every 15 minutes or 16 MiB. Completed chunks are verified and compressed with gzip. The active chunk remains uncompressed so an interrupted process leaves a directly readable tail.

When the logs exceed their quota, the ground station removes the oldest completed or interrupted sessions until usage is below 90% of the quota. For an unusually long active session, it removes its oldest completed chunks but preserves the current chunk and at least the two newest completed chunks of each kind.

Session ownership and quota operations use advisory file locks. Multiple ground-station processes can safely share one logs directory: recovery and pruning skip every session that is still owned by a running process.

## SBB raw format

All integers are big-endian. A file starts with:

| Size | Field |
| ---: | --- |
| 8 | Magic `SWTXSBB1` |
| 8 | Session start UTC, nanoseconds since Unix epoch |

Each serial record contains:

| Size | Field |
| ---: | --- |
| 1 | Record kind (`1` for serial bytes) |
| 8 | Nanoseconds elapsed since session start |
| 8 | Receive UTC in nanoseconds |
| 4 | Connection ID |
| 2 | Data length |
| variable | Serial bytes |
| 4 | CRC-32 over the record header and serial bytes |

`iter_sbb_records()` in `blackbox.py` reads compressed or uncompressed chunks and validates every CRC.

## Failure behavior

Serial monitoring continues if the logs directory becomes unavailable or a write fails. The dashboard displays `BLACKBOX WRITE FAILED`. Compression failure retains the original uncompressed chunk. A session left active by a crash is marked `interrupted` on the next launch and remains readable. During a clean shutdown, the recorder drains and syncs the writer before it finishes compression and marks the session complete.
