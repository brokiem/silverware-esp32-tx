#!/usr/bin/env python3
"""Silverware TX ground station with live telemetry and blackbox recording."""

from __future__ import annotations

import argparse
import dataclasses
import json
import math
import os
import re
import shutil
import struct
import sys
import textwrap
import time
from pathlib import Path
from typing import Dict, Iterable, List, Optional, TextIO, Tuple

from blackbox import BlackboxConfig, BlackboxRecorder, DisabledBlackbox, inspect_session, parse_storage_size


BRIDGE_VERSION = 1
MESSAGE_RAW_FC = 1
MESSAGE_LOCAL_STATE = 2
RAW_FC_LENGTH = 15
LOCAL_STATE_LENGTH = 44
SERIAL_READ_TIMEOUT_S = 0.05
SERIAL_RETRY_DELAY_S = 0.25
SERIAL_SILENCE_TIMEOUT_S = 3.0

SYSTEM_STATES = {
    0: "BOOT",
    1: "WAIT GAMEPAD",
    2: "BINDING",
    3: "LOCKED",
    4: "ACTIVE",
    5: "GAMEPAD FAILSAFE",
    6: "RADIO ERROR",
    7: "PREARM MODE",
}

STATUS_FLAGS = (
    "GAMEPAD CONNECTED",
    "GAMEPAD FRESH",
    "SAFETY LOCKED",
    "CONTROL ENABLED",
    "BINDING",
    "GAMEPAD FAILSAFE",
    "RADIO ERROR",
    "RADIO INITIALIZED",
    "FC TELEMETRY SEEN",
    "FC TELEMETRY FRESH",
    "FC TELEMETRY STALE",
)

BUTTON_NAMES = (
    "A",
    "B",
    "X",
    "Y",
    "LB",
    "RB",
    "L3",
    "R3",
    "Start",
    "View",
    "D-pad Up",
    "D-pad Down",
    "D-pad Left",
    "D-pad Right",
)

AUX_NAMES = ("Level", "Race", "Horizon", "PID 2", "LEDs")
PAGE_NAMES = ("A/control", "B/flight", "C/power", "D/system")


class DecodeError(ValueError):
    """A frame is malformed or fails an integrity check."""


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def cobs_decode(encoded: bytes) -> bytes:
    if not encoded:
        raise DecodeError("empty COBS frame")
    decoded = bytearray()
    index = 0
    while index < len(encoded):
        code = encoded[index]
        if code == 0:
            raise DecodeError("zero byte inside COBS frame")
        index += 1
        end = index + code - 1
        if end > len(encoded):
            raise DecodeError("truncated COBS block")
        decoded.extend(encoded[index:end])
        index = end
        if code != 0xFF and index < len(encoded):
            decoded.append(0)
    return bytes(decoded)


@dataclasses.dataclass(frozen=True)
class BridgeRecord:
    message_type: int
    sequence: int
    timestamp_us: int
    payload: bytes


def decode_bridge_frame(encoded: bytes) -> BridgeRecord:
    record = cobs_decode(encoded)
    if len(record) < 16:
        raise DecodeError("bridge record is too short")
    version, message_type, payload_length, sequence, timestamp_us = struct.unpack_from(">BBHHQ", record)
    if version != BRIDGE_VERSION:
        raise DecodeError(f"unsupported bridge version {version}")
    expected_payload_length = {MESSAGE_RAW_FC: RAW_FC_LENGTH, MESSAGE_LOCAL_STATE: LOCAL_STATE_LENGTH}.get(message_type)
    if expected_payload_length is None:
        raise DecodeError(f"unsupported message type {message_type}")
    if payload_length != expected_payload_length:
        raise DecodeError(f"wrong type-{message_type} payload length {payload_length}")
    if len(record) != 14 + payload_length + 2:
        raise DecodeError("record length does not match its header")
    expected_crc = struct.unpack_from(">H", record, len(record) - 2)[0]
    actual_crc = crc16_ccitt_false(record[:-2])
    if actual_crc != expected_crc:
        raise DecodeError(f"bridge CRC mismatch: got {expected_crc:04X}, expected {actual_crc:04X}")
    return BridgeRecord(message_type, sequence, timestamp_us, record[14:-2])


class BitReader:
    def __init__(self, data: bytes):
        self.data = data
        self.offset = 0

    def read(self, width: int) -> int:
        value = 0
        for _ in range(width):
            byte_index = self.offset >> 3
            bit_index = 7 - (self.offset & 7)
            if byte_index >= len(self.data):
                raise DecodeError("telemetry bit field exceeds payload")
            value = (value << 1) | ((self.data[byte_index] >> bit_index) & 1)
            self.offset += 1
        return value

    def signed(self, width: int) -> int:
        value = self.read(width)
        return value - (1 << width) if value & (1 << (width - 1)) else value


@dataclasses.dataclass
class FcTelemetry:
    protocol: str = "Never"
    page_mask: int = 0
    system_subpage_mask: int = 0
    armed: bool = False
    failsafe: bool = False
    on_ground: bool = False
    idle_up: bool = False
    low_voltage: bool = False
    pid_profile: bool = False
    flight_mode: str = "Unknown"
    sequence: int = 0
    telemetry_packet_losses: int = 0
    battery_raw_v: float = 0.0
    battery_compensated_v: float = 0.0
    receiver_packets_per_second: int = 0
    pid_p: int = 0
    pid_i: int = 0
    pid_d: int = 0
    roll_deg: float = 0.0
    pitch_deg: float = 0.0
    relative_yaw_deg: float = 0.0
    accel_x_g: float = 0.0
    accel_y_g: float = 0.0
    accel_z_g: float = 0.0
    gyro_roll_dps: float = 0.0
    gyro_pitch_dps: float = 0.0
    gyro_yaw_dps: float = 0.0
    setpoint_roll_dps: float = 0.0
    setpoint_pitch_dps: float = 0.0
    setpoint_yaw_dps: float = 0.0
    commanded_throttle_percent: float = 0.0
    applied_throttle_percent: float = 0.0
    motors_percent: List[float] = dataclasses.field(default_factory=lambda: [0.0] * 4)
    flight_time_seconds: int = 0
    packets_lost_per_second: int = 0
    link_quality_percent: int = 0
    maximum_rx_gap_ms: float = 0.0
    current_rx_gap_ms: float = 0.0
    failsafe_count: int = 0
    loop_time_average_us: int = 0
    loop_time_maximum_us: int = 0
    loop_overrun_count: int = 0
    imu_temperature_raw: int = 0
    imu_type: int = 0
    cpu_load_percent: int = 0
    telemetry_tx_count: int = 0
    receiver_packet_total: int = 0
    estimated_lost_packet_total: int = 0
    page_received_at: Dict[int, float] = dataclasses.field(default_factory=dict)
    _has_fc_sequence: bool = False
    _last_fc_sequence: int = 0

    def _clear_extended(self) -> None:
        fresh = FcTelemetry(protocol=self.protocol)
        self.__dict__.update(fresh.__dict__)

    def update(self, packet: bytes, received_at: float) -> None:
        if len(packet) != RAW_FC_LENGTH or packet[0] not in (0x85, 0x86):
            raise DecodeError("unknown Bayang telemetry packet")
        if (sum(packet[:14]) & 0xFF) != packet[14]:
            raise DecodeError("Bayang checksum mismatch")

        protocol = "Extended V1" if packet[0] == 0x86 else "Original"
        if protocol != self.protocol:
            self.protocol = protocol
            self._clear_extended()

        if packet[0] == 0x85:
            self.battery_raw_v = (((packet[3] & 0x07) << 8) | packet[4]) / 100.0
            self.battery_compensated_v = (((packet[5] & 0x07) << 8) | packet[6]) / 100.0
            self.low_voltage = bool((packet[3] >> 3) & 1)
            self.receiver_packets_per_second = packet[7] * 2
            pid_term = packet[8] >> 6
            pid_value = ((packet[8] & 0x3F) << 8) | packet[9]
            if pid_term == 0:
                self.pid_p = pid_value
            elif pid_term == 1:
                self.pid_i = pid_value
            elif pid_term == 2:
                self.pid_d = pid_value
            return

        common = packet[1]
        page = common >> 6
        sequence = common & 0x0F
        self.armed = bool(common & 0x20)
        self.failsafe = bool(common & 0x10)
        if self._has_fc_sequence:
            expected = (self._last_fc_sequence + 1) & 0x0F
            self.telemetry_packet_losses += (sequence - expected) & 0x0F
        self.sequence = sequence
        self._last_fc_sequence = sequence
        self._has_fc_sequence = True

        reader = BitReader(packet[2:14])
        if page == 0:
            self._parse_control(reader)
        elif page == 1:
            self._parse_flight(reader)
        elif page == 2:
            self._parse_power(reader)
        else:
            self._parse_system(reader)
        self.page_mask |= 1 << page
        self.page_received_at[page] = received_at

    def _parse_control(self, reader: BitReader) -> None:
        self.gyro_roll_dps, self.gyro_pitch_dps, self.gyro_yaw_dps = [reader.signed(10) * 4.0 for _ in range(3)]
        self.setpoint_roll_dps, self.setpoint_pitch_dps, self.setpoint_yaw_dps = [
            reader.signed(10) * 4.0 for _ in range(3)
        ]
        self.commanded_throttle_percent = reader.read(6) * 100.0 / 63.0
        self.applied_throttle_percent = reader.read(6) * 100.0 / 63.0
        self.motors_percent = [reader.read(6) * 100.0 / 63.0 for _ in range(4)]

    def _parse_flight(self, reader: BitReader) -> None:
        self.roll_deg = reader.signed(12) * 0.1
        self.pitch_deg = reader.signed(12) * 0.1
        self.relative_yaw_deg = reader.signed(12) * 0.1
        self.accel_x_g = reader.signed(12) / 256.0
        self.accel_y_g = reader.signed(12) / 256.0
        self.accel_z_g = reader.signed(12) / 256.0
        self.flight_time_seconds = reader.read(16)
        flags = reader.read(8)
        self.on_ground = bool(flags & 0x01)
        self.idle_up = bool(flags & 0x02)
        self.low_voltage = bool(flags & 0x04)
        self.pid_profile = bool(flags & 0x40)
        level, race, horizon = bool(flags & 0x08), bool(flags & 0x10), bool(flags & 0x20)
        if not level:
            self.flight_mode = "Acro"
        elif race and horizon:
            self.flight_mode = "RaceHorizon"
        elif horizon:
            self.flight_mode = "Horizon"
        elif race:
            self.flight_mode = "Race"
        else:
            self.flight_mode = "Level"

    def _parse_power(self, reader: BitReader) -> None:
        self.battery_raw_v = reader.read(16) / 1000.0
        self.battery_compensated_v = reader.read(16) / 1000.0
        self.receiver_packets_per_second = reader.read(8)
        self.packets_lost_per_second = reader.read(8)
        self.link_quality_percent = reader.read(8)
        self.low_voltage = bool(reader.read(8) & 1)
        self.maximum_rx_gap_ms = reader.read(16) * 0.1
        self.current_rx_gap_ms = reader.read(8) * 0.1
        self.failsafe_count = reader.read(8)

    def _parse_system(self, reader: BitReader) -> None:
        counters = bool(reader.read(1))
        if not counters:
            self.loop_time_average_us = reader.read(16)
            self.loop_time_maximum_us = reader.read(16)
            self.loop_overrun_count = reader.read(16)
            self.imu_temperature_raw = reader.signed(16)
            self.imu_type = reader.read(8)
            self.cpu_load_percent = reader.read(8)
            self.telemetry_tx_count = reader.read(15)
            self.system_subpage_mask |= 1
        else:
            self.receiver_packet_total = reader.read(32)
            self.estimated_lost_packet_total = reader.read(32)
            self.telemetry_tx_count = reader.read(31)
            self.system_subpage_mask |= 2


@dataclasses.dataclass
class LocalState:
    system_state: int
    status_flags: int
    buttons: int
    aux_modes: int
    consecutive_tx_failures: int
    next_hopping_channel: int
    roll_raw: int
    pitch_raw: int
    yaw_raw: int
    throttle_raw: int
    gamepad_age_ms: int
    fc_telemetry_age_ms: int
    tx_packets: int
    tx_failures: int
    telemetry_accepted: int
    telemetry_rejected: int
    deadline_misses: int
    export_queue_drops: int

    @classmethod
    def decode(cls, payload: bytes) -> "LocalState":
        if len(payload) != LOCAL_STATE_LENGTH:
            raise DecodeError("wrong local-state payload length")
        return cls(*struct.unpack(">BHHBBBhhhHHHIIIIII", payload))


@dataclasses.dataclass
class DecoderStats:
    valid_frames: int = 0
    malformed_frames: int = 0
    invalid_bayang_packets: int = 0
    type1_sequence_gaps: int = 0
    type2_sequence_gaps: int = 0


@dataclasses.dataclass(frozen=True)
class ModelUpdate:
    received_at: float
    message_type: int
    sequence_gap: int
    esp_restarted: bool
    fc_page: Optional[int]


class TelemetryModel:
    def __init__(self) -> None:
        self.fc = FcTelemetry()
        self.local: Optional[LocalState] = None
        self.stats = DecoderStats()
        self.last_type1_at: Optional[float] = None
        self.last_type2_at: Optional[float] = None
        self.last_any_at: Optional[float] = None
        self._last_sequence: Dict[int, int] = {}
        self._last_esp_timestamp: Optional[int] = None

    def accept(self, record: BridgeRecord, now: Optional[float] = None) -> ModelUpdate:
        received_at = time.monotonic() if now is None else now
        esp_restarted = self._last_esp_timestamp is not None and record.timestamp_us < self._last_esp_timestamp
        if esp_restarted:
            self._last_sequence.clear()
            self.fc = FcTelemetry()
            self.local = None
            self.last_type1_at = None
            self.last_type2_at = None
        self._last_esp_timestamp = record.timestamp_us

        previous = self._last_sequence.get(record.message_type)
        gap = 0
        if previous is not None:
            gap = (record.sequence - previous - 1) & 0xFFFF
            if record.message_type == MESSAGE_RAW_FC:
                self.stats.type1_sequence_gaps += gap
            else:
                self.stats.type2_sequence_gaps += gap
        self._last_sequence[record.message_type] = record.sequence

        if record.message_type == MESSAGE_RAW_FC:
            try:
                self.fc.update(record.payload, received_at)
            except DecodeError:
                self.stats.invalid_bayang_packets += 1
                raise
            self.last_type1_at = received_at
        else:
            self.local = LocalState.decode(record.payload)
            self.last_type2_at = received_at
        self.last_any_at = received_at
        self.stats.valid_frames += 1
        fc_page = record.payload[1] >> 6 if record.message_type == MESSAGE_RAW_FC and record.payload[0] == 0x86 else None
        return ModelUpdate(received_at, record.message_type, gap, esp_restarted, fc_page)


def human_bytes(value: int) -> str:
    amount = float(max(0, value))
    for unit in ("B", "KiB", "MiB", "GiB", "TiB"):
        if amount < 1024.0 or unit == "TiB":
            return f"{amount:.0f} {unit}" if unit in ("B", "KiB") else f"{amount:.1f} {unit}"
        amount /= 1024.0
    return f"{amount:.1f} TiB"


def print_session_inspection(report: Dict[str, object], json_output: bool = False) -> int:
    """Print a compact post-flight report and return a verification exit status."""

    if json_output:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        duration = report.get("duration_s")
        duration_text = f"{float(duration):.1f} s" if duration is not None else "unknown"
        state = str(report.get("state", "unknown")) + (" (currently open)" if report.get("live") else "")
        battery = report.get("minimum_battery_v")
        battery_text = f"{float(battery):.3f} V" if battery is not None else "not available"
        gap = report.get("maximum_rx_gap_ms")
        gap_text = f"{float(gap):.1f} ms" if gap is not None else "not available"
        modes = ", ".join(str(item) for item in report.get("modes", [])) or "not available"
        print(f"BLACKBOX  {report['session_dir']}")
        print(f"State     {state}")
        print(f"Time      {report.get('start_utc') or '--'}  ->  {report.get('end_utc') or '--'}  ({duration_text})")
        print(f"Storage   {human_bytes(int(report.get('disk_bytes', 0)))}")
        print(
            f"Raw       {report.get('raw_records', 0)} records / {human_bytes(int(report.get('raw_bytes', 0)))}"
            f" across {report.get('raw_files', 0)} file(s)"
        )
        print(f"Telemetry {report.get('csv_rows', 0)} rows across {report.get('csv_files', 0)} file(s)")
        print(f"Flight    modes {modes}  |  minimum battery {battery_text}  |  maximum RX gap {gap_text}")
        severities = report.get("severity_counts", {})
        if isinstance(severities, dict):
            severity_text = ", ".join(f"{name}={count}" for name, count in severities.items()) or "none"
            print(f"Events    {severity_text}")
        warnings = report.get("recent_warnings", [])
        if warnings:
            print("Warnings  most recent:")
            for item in warnings:
                print(f"  {item.get('utc') or '--'}  {item.get('type')}: {item.get('message')}")
        errors = list(report.get("integrity_errors", []))
        if errors:
            print("Integrity FAILED")
            for error in errors:
                print(f"  {error}")
        else:
            print("Integrity OK (all available raw records passed CRC validation)")
        metadata_errors = report.get("metadata_errors", [])
        if metadata_errors:
            print("Recorder errors:")
            for error in metadata_errors:
                print(f"  {error}")
    return 1 if report.get("integrity_errors") else 0


def telemetry_snapshot(
    model: TelemetryModel,
    port: str,
    connection_id: int,
    now: float,
) -> Dict[str, object]:
    """Flatten the complete aggregate model into a stable CSV row."""
    fc = model.fc
    row: Dict[str, object] = {
        "connection_id": connection_id,
        "port": port,
        "bridge_live": model.last_any_at is not None and now - model.last_any_at <= 0.5,
        "local_live": model.last_type2_at is not None and now - model.last_type2_at <= 0.25,
        "fc_live": model.last_type1_at is not None and now - model.last_type1_at <= 0.5,
    }
    for field in dataclasses.fields(FcTelemetry):
        if field.name.startswith("_") or field.name == "page_received_at":
            continue
        value = getattr(fc, field.name)
        if field.name == "motors_percent":
            for index, motor in enumerate(value, 1):
                row[f"fc_motor_{index}_percent"] = motor
        else:
            row[f"fc_{field.name}"] = value
    for page in range(4):
        received = fc.page_received_at.get(page)
        row[f"fc_page_{page}_age_ms"] = "" if received is None else max(0.0, now - received) * 1000.0

    local = model.local
    for field in dataclasses.fields(LocalState):
        row[f"local_{field.name}"] = "" if local is None else getattr(local, field.name)
    for field in dataclasses.fields(DecoderStats):
        row[f"stats_{field.name}"] = getattr(model.stats, field.name)
    return row


def blackbox_sample_period(model: TelemetryModel) -> Optional[float]:
    if model.last_any_at is None:
        return None
    control_enabled = model.local is not None and bool(model.local.status_flags & (1 << 3))
    return 0.1 if model.fc.armed or control_enabled else 0.5


class FlightEventTracker:
    """Convert telemetry state changes and health timeouts into blackbox events."""

    def __init__(self) -> None:
        self.previous: Dict[str, object] = {}
        self.initial_state_logged = False

    def _change(
        self,
        recorder: object,
        key: str,
        value: object,
        message: str,
        danger_when: Optional[object] = None,
    ) -> None:
        old = self.previous.get(key, dataclasses.MISSING)
        self.previous[key] = value
        if old is dataclasses.MISSING:
            if danger_when is None or value != danger_when:
                return
        elif old == value:
            return
        severity = "warning" if danger_when is not None and value == danger_when else "info"
        recorder.event(
            key,
            severity,
            message,
            {"previous": None if old is dataclasses.MISSING else old, "current": value},
        )

    def accept(self, recorder: object, model: TelemetryModel, update: ModelUpdate) -> None:
        if update.esp_restarted:
            recorder.event("esp_reboot", "warning", "ESP timestamp restarted", {})
        if update.sequence_gap:
            recorder.event(
                "bridge_sequence_gap",
                "warning",
                f"Bridge message type {update.message_type} skipped {update.sequence_gap} record(s)",
                {"message_type": update.message_type, "gap": update.sequence_gap},
            )

        fc = model.fc
        if fc.protocol == "Extended V1":
            self._change(recorder, "armed", fc.armed, "FC arm state changed", True)
            self._change(recorder, "fc_failsafe", fc.failsafe, "FC failsafe state changed", True)
            if fc.page_mask & (1 << 1):
                self._change(recorder, "flight_mode", fc.flight_mode, f"Flight mode changed to {fc.flight_mode}")
        if fc.protocol != "Never":
            self._change(recorder, "low_voltage", fc.low_voltage, "Low-voltage state changed", True)

        local = model.local
        if not self.initial_state_logged and (fc.protocol != "Never" or local is not None):
            recorder.event(
                "initial_state",
                "info",
                "Initial telemetry state acquired",
                {
                    "protocol": fc.protocol,
                    "armed": fc.armed if fc.protocol == "Extended V1" else None,
                    "flight_mode": fc.flight_mode if fc.page_mask & (1 << 1) else None,
                    "system_state": None
                    if local is None
                    else SYSTEM_STATES.get(local.system_state, f"UNKNOWN({local.system_state})"),
                    "status_flags": None if local is None else local.status_flags,
                },
            )
            self.initial_state_logged = True
        if local is not None:
            state = SYSTEM_STATES.get(local.system_state, f"UNKNOWN({local.system_state})")
            self._change(recorder, "system_state", state, f"Transmitter state changed to {state}")
            flags = local.status_flags
            self._change(recorder, "control_enabled", bool(flags & (1 << 3)), "Control enable state changed", False)
            self._change(recorder, "gamepad_connected", bool(flags & 1), "Gamepad connection state changed", False)
            self._change(recorder, "gamepad_fresh", bool(flags & (1 << 1)), "Gamepad freshness state changed", False)
            self._change(recorder, "binding", bool(flags & (1 << 4)), "Binding state changed", True)
            self._change(recorder, "gamepad_failsafe", bool(flags & (1 << 5)), "Gamepad failsafe changed", True)
            self._change(recorder, "radio_error", bool(flags & (1 << 6)), "Radio error state changed", True)

    def health(self, recorder: object, model: TelemetryModel, now: float) -> None:
        states = {
            "bridge_live": model.last_any_at is not None and now - model.last_any_at <= 0.5,
            "local_live": model.last_type2_at is not None and now - model.last_type2_at <= 0.25,
            "fc_live": model.last_type1_at is not None and now - model.last_type1_at <= 0.5,
        }
        for key, live in states.items():
            old = self.previous.get(key, dataclasses.MISSING)
            self.previous[key] = live
            if old is dataclasses.MISSING:
                if live:
                    recorder.event(f"{key}_acquired", "info", f"{key.replace('_', ' ')} acquired", {})
                continue
            if old == live:
                continue
            event_type = f"{key}_{'recovered' if live else 'lost'}"
            severity = "info" if live else "warning"
            recorder.event(event_type, severity, event_type.replace("_", " "), {})


def set_names(value: int, names: Iterable[str]) -> str:
    active = [name for bit, name in enumerate(names) if value & (1 << bit)]
    return ", ".join(active) if active else "none"


def age_text(value: int) -> str:
    return "never" if value == 0xFFFF else f"{value} ms"


def flight_time_text(seconds: int) -> str:
    return f"{seconds // 3600:02d}:{seconds // 60 % 60:02d}:{seconds % 60:02d}"


ANSI_RE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")


def strip_ansi(text: str) -> str:
    return ANSI_RE.sub("", text)


def visible_len(text: str) -> int:
    return len(strip_ansi(text))


def monotonic_age_text(last_at: Optional[float], now: float) -> str:
    if last_at is None:
        return "never"
    age = max(0.0, now - last_at)
    if age < 1.0:
        return f"{age * 1000:.0f} ms"
    if age < 60.0:
        return f"{age:.1f} s"
    return f"{age / 60.0:.1f} min"


class TerminalCanvas:
    """Low-resolution fallback rasterizer for terminals without Braille support."""

    def __init__(self, width: int, height: int):
        self.width = max(1, width)
        self.height = max(1, height)
        self.cells = [[" " for _ in range(self.width)] for _ in range(self.height)]
        self.priority = [[-1 for _ in range(self.width)] for _ in range(self.height)]

    @staticmethod
    def _line_char(dx: int, dy: int) -> str:
        if abs(dx) >= 3 * abs(dy):
            return "-"
        if abs(dy) >= 2 * abs(dx):
            return "|"
        return "\\" if dx * dy >= 0 else "/"

    def put(self, x: int, y: int, char: str, priority: int = 1) -> None:
        if not (0 <= x < self.width and 0 <= y < self.height):
            return
        previous = self.priority[y][x]
        if priority < previous:
            return
        if priority == previous and priority <= 1 and self.cells[y][x] not in (" ", char):
            char = "+"
        self.cells[y][x] = char
        self.priority[y][x] = priority

    def line(self, x0: int, y0: int, x1: int, y1: int, priority: int = 1) -> None:
        dx_total = x1 - x0
        dy_total = y1 - y0
        char = self._line_char(dx_total, dy_total)
        dx = abs(dx_total)
        sx = 1 if x0 < x1 else -1
        dy = -abs(dy_total)
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        x, y = x0, y0
        while True:
            self.put(x, y, char, priority)
            if x == x1 and y == y1:
                break
            twice = 2 * err
            if twice >= dy:
                err += dy
                x += sx
            if twice <= dx:
                err += dx
                y += sy

    def rows(self) -> List[str]:
        return ["".join(row).rstrip() for row in self.cells]


class BrailleCanvas:
    """High-resolution vector canvas encoded into Unicode Braille cells.

    One terminal character stores a 2x4 dot raster, so the quad can use eight
    addressable subpixels per cell while remaining plain terminal text.
    """

    DOT_BITS = {
        (0, 0): 0x01,
        (0, 1): 0x02,
        (0, 2): 0x04,
        (0, 3): 0x40,
        (1, 0): 0x08,
        (1, 1): 0x10,
        (1, 2): 0x20,
        (1, 3): 0x80,
    }

    def __init__(self, width: int, height: int):
        self.width_chars = max(1, width)
        self.height_chars = max(1, height)
        self.width = self.width_chars * 2
        self.height = self.height_chars * 4
        self.pixels = set()

    def dot(self, x: int, y: int) -> None:
        if 0 <= x < self.width and 0 <= y < self.height:
            self.pixels.add((x, y))

    def line(self, x0: int, y0: int, x1: int, y1: int) -> None:
        dx = abs(x1 - x0)
        sx = 1 if x0 < x1 else -1
        dy = -abs(y1 - y0)
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        x, y = x0, y0
        while True:
            self.dot(x, y)
            if x == x1 and y == y1:
                break
            twice = 2 * err
            if twice >= dy:
                err += dy
                x += sx
            if twice <= dx:
                err += dx
                y += sy

    def filled_dot(self, x: int, y: int, radius: int = 1) -> None:
        radius_sq = radius * radius
        for oy in range(-radius, radius + 1):
            for ox in range(-radius, radius + 1):
                if ox * ox + oy * oy <= radius_sq:
                    self.dot(x + ox, y + oy)

    def rows(self) -> List[str]:
        output: List[str] = []
        for cell_y in range(self.height_chars):
            row: List[str] = []
            for cell_x in range(self.width_chars):
                bits = 0
                for dot_x in range(2):
                    for dot_y in range(4):
                        if (cell_x * 2 + dot_x, cell_y * 4 + dot_y) in self.pixels:
                            bits |= self.DOT_BITS[(dot_x, dot_y)]
                row.append(chr(0x2800 + bits) if bits else " ")
            output.append("".join(row).rstrip())
        return output


class Dashboard:
    """Responsive, low-flicker terminal dashboard for live telemetry."""

    BORDER_CODE = "2;36"
    TITLE_CODE = "1;36"
    LABEL_CODE = "2"
    VALUE_CODE = "1"
    GOOD_CODE = "1;32"
    WARN_CODE = "1;33"
    BAD_CODE = "1;31"
    INFO_CODE = "1;36"
    FIELD_VALUE_COLUMN = 20

    def __init__(self, use_color: bool = True):
        self.use_color = use_color
        self.use_unicode = self._unicode_supported()

    @staticmethod
    def _unicode_supported() -> bool:
        encoding = getattr(sys.stdout, "encoding", None) or "utf-8"
        try:
            "─│╱╲┼○◆▲↖⣿⠿".encode(encoding)
            return True
        except (LookupError, UnicodeEncodeError):
            return False

    def color(self, text: str, code: str) -> str:
        return f"\033[{code}m{text}\033[0m" if self.use_color else text

    def badge(self, label: str, value: str, code: str) -> str:
        return self.color(f"[{label}:{value}]", code)

    def bool_text(self, value: bool, true_text: str = "YES", false_text: str = "NO", *, warn_when_true: bool = False) -> str:
        if warn_when_true:
            return self.color(true_text if value else false_text, self.BAD_CODE if value else self.GOOD_CODE)
        return self.color(true_text if value else false_text, self.GOOD_CODE if value else self.LABEL_CODE)

    def field(self, label: str, value: object) -> str:
        """Place every panel value at one shared, predictable column."""
        return f"{label:<{self.FIELD_VALUE_COLUMN}}{value}"

    @staticmethod
    def _align(text: str, width: int, alignment: str = "left") -> str:
        """Align ANSI-colored text using its visible rather than encoded width."""
        padding = max(0, width - visible_len(text))
        if alignment == "right":
            return (" " * padding) + text
        if alignment == "center":
            left = padding // 2
            return (" " * left) + text + (" " * (padding - left))
        return text + (" " * padding)

    def _spread(self, left: str, right: str, width: int) -> str:
        """Place two items at opposite sides of a fixed-width row."""
        gap = max(2, width - visible_len(left) - visible_len(right))
        return left + (" " * gap) + right

    @staticmethod
    def _terminal_width() -> int:
        try:
            # Leave the terminal's final column unused. Writing into that cell
            # enables delayed wrapping in many terminals and can shift the next
            # row before the following refresh has a chance to repair it.
            return max(8, shutil.get_terminal_size(fallback=(120, 40)).columns - 1)
        except OSError:
            return 119

    def _fit(self, text: str, width: int) -> str:
        return text + (" " * max(0, width - visible_len(text)))

    def _wrap_line(self, line: str, width: int) -> List[str]:
        if width <= 1 or visible_len(line) <= width:
            return [line]
        plain = strip_ansi(line)
        return textwrap.wrap(
            plain,
            width=width,
            subsequent_indent="  ",
            break_long_words=True,
            break_on_hyphens=False,
        ) or [""]

    def panel(self, title: str, rows: Iterable[str], width: int) -> List[str]:
        width = max(8, width)
        inner = width - 2
        content_width = width - 4
        title_text = f" {title} "
        if len(title_text) > inner:
            title_text = f" {title[:max(1, inner - 3)]} "
        top_plain = "+" + title_text + ("-" * max(0, inner - len(title_text))) + "+"
        bottom_plain = "+" + ("-" * inner) + "+"

        output = [self.color(top_plain, self.BORDER_CODE)]
        for row in rows:
            for wrapped in self._wrap_line(row, content_width):
                output.append("| " + self._fit(wrapped, content_width) + " |")
        output.append(self.color(bottom_plain, self.BORDER_CODE))
        return output

    def _grow_panel(self, panel: List[str], width: int, target_height: int) -> List[str]:
        """Grow a panel vertically by adding empty content rows before its bottom border."""
        if len(panel) >= target_height or len(panel) < 2:
            return panel

        content_width = max(0, width - 4)
        blank_row = "| " + (" " * content_width) + " |"
        extra = target_height - len(panel)
        return panel[:-1] + ([blank_row] * extra) + [panel[-1]]

    @staticmethod
    def _join_columns(left: List[str], right: List[str], left_width: int, right_width: int, gap: int = 2) -> List[str]:
        height = max(len(left), len(right))
        left_pad = " " * left_width
        right_pad = " " * right_width
        output: List[str] = []
        for index in range(height):
            lhs = left[index] if index < len(left) else left_pad
            rhs = right[index] if index < len(right) else right_pad
            output.append(lhs + (" " * gap) + rhs)
        return output

    @staticmethod
    def _join_many_columns(panels: List[List[str]], widths: List[int], gap: int = 2) -> List[str]:
        height = max((len(panel) for panel in panels), default=0)
        output: List[str] = []
        for row in range(height):
            parts = []
            for panel, width in zip(panels, widths):
                parts.append(panel[row] if row < len(panel) else (" " * width))
            output.append((" " * gap).join(parts))
        return output

    def _page_age(self, fc: FcTelemetry, page: int, now: float) -> str:
        return monotonic_age_text(fc.page_received_at.get(page), now)

    def _alert_line(
        self,
        model: TelemetryModel,
        local: Optional[LocalState],
        bridge_live: bool,
        local_live: bool,
        fc_live: bool,
        blackbox_status: Optional[Dict[str, object]] = None,
    ) -> str:
        alerts: List[Tuple[str, str]] = []
        fc = model.fc

        if model.last_any_at is None:
            return self.color("STATUS  WAITING - no bridge telemetry received yet", self.WARN_CODE)
        if not bridge_live:
            return self.color("ALERTS  ! BRIDGE OFFLINE - telemetry values are frozen", self.BAD_CODE)

        if local is None:
            alerts.append(("WAITING FOR LOCAL STATE", self.WARN_CODE))
        elif not local_live:
            alerts.append(("LOCAL STATE STALE", self.WARN_CODE))

        if fc.protocol == "Never":
            alerts.append(("WAITING FOR FC TELEMETRY", self.WARN_CODE))
        elif not fc_live:
            alerts.append(("FC TELEMETRY LOST", self.BAD_CODE))

        if fc_live and fc.protocol != "Never" and fc.failsafe:
            alerts.append(("FC FAILSAFE", self.BAD_CODE))
        if fc_live and fc.protocol != "Never" and fc.low_voltage:
            alerts.append(("LOW VOLTAGE", self.BAD_CODE))

        if local_live and local is not None:
            if local.status_flags & (1 << 5):
                alerts.append(("GAMEPAD FAILSAFE", self.BAD_CODE))
            if local.status_flags & (1 << 6):
                alerts.append(("RADIO ERROR", self.BAD_CODE))
            if local.status_flags & (1 << 10):
                alerts.append(("FC DATA STALE", self.WARN_CODE))

        if blackbox_status and blackbox_status.get("error"):
            alerts.append(("BLACKBOX WRITE FAILED", self.BAD_CODE))

        if not alerts:
            return ""

        rendered = "  ".join(self.color(f"! {name}", code) for name, code in alerts)
        return self.color("ALERTS  ", self.BAD_CODE) + rendered

    def _header(
        self,
        model: TelemetryModel,
        port: str,
        now: float,
        width: int,
        bridge_live: bool,
        local_live: bool,
        fc_live: bool,
        blackbox_status: Optional[Dict[str, object]] = None,
    ) -> List[str]:
        fc = model.fc
        local = model.local

        bridge_seen = model.last_any_at is not None
        bridge_badge = self.badge(
            "BRIDGE",
            "LIVE" if bridge_live else ("WAIT" if not bridge_seen else "OFFLINE"),
            self.GOOD_CODE if bridge_live else self.WARN_CODE if not bridge_seen else self.BAD_CODE,
        )
        local_badge = self.badge(
            "LOCAL",
            "LIVE" if local_live else ("WAIT" if local is None else "STALE"),
            self.GOOD_CODE if local_live else self.WARN_CODE,
        )
        fc_badge = self.badge(
            "FC",
            "LIVE" if fc_live else ("WAIT" if fc.protocol == "Never" else "LOST"),
            self.GOOD_CODE if fc_live else self.WARN_CODE if fc.protocol == "Never" else self.BAD_CODE,
        )

        if local is None or not local_live:
            control_badge = self.badge("CONTROL", "--", self.LABEL_CODE)
        else:
            control_enabled = bool(local.status_flags & (1 << 3))
            control_badge = self.badge(
                "CONTROL",
                "ENABLED" if control_enabled else "LOCKED",
                self.INFO_CODE if control_enabled else self.WARN_CODE,
            )

        if fc.protocol != "Extended V1" or not fc_live:
            arm_badge = self.badge("ARM", "--", self.LABEL_CODE)
            mode_badge = self.badge("MODE", "--", self.LABEL_CODE)
        else:
            arm_badge = self.badge(
                "ARM",
                "ARMED" if fc.armed else "DISARMED",
                self.WARN_CODE if fc.armed else self.GOOD_CODE,
            )
            mode_value = fc.flight_mode.upper() if (fc.page_mask & (1 << 1)) else "--"
            mode_badge = self.badge("MODE", mode_value, self.INFO_CODE if mode_value != "--" else self.LABEL_CODE)

        power_known = fc.protocol == "Original" or (fc.protocol == "Extended V1" and bool(fc.page_mask & (1 << 2)))
        if fc_live and power_known:
            battery_code = self.BAD_CODE if fc.low_voltage else self.VALUE_CODE
            battery_badge = self.badge("BAT", f"{fc.battery_compensated_v:.3f}V", battery_code)
        else:
            battery_badge = self.badge("BAT", "--.--V", self.LABEL_CODE)

        title = self.color("SILVERWARE TX / GROUND STATION", self.TITLE_CODE)
        if not blackbox_status or not blackbox_status.get("enabled"):
            recorder_text = "REC OFF"
        elif blackbox_status.get("error"):
            recorder_text = "REC ERROR"
        else:
            session_usage = human_bytes(int(blackbox_status.get("session_usage", 0)))
            usage = human_bytes(int(blackbox_status.get("total_usage", 0)))
            quota = int(blackbox_status.get("quota_bytes", 0))
            recorder_text = f"REC {session_usage}  |  LOGS {usage}" + (f"/{human_bytes(quota)}" if quota else "")
        meta = (
            f"Port {port}  |  RX age {monotonic_age_text(model.last_any_at, now)}"
            f"  |  {recorder_text}  |  Ctrl+C exit"
        )
        link_badges = "  ".join((bridge_badge, local_badge, fc_badge))
        flight_badges = "  ".join((arm_badge, mode_badge, battery_badge))
        alert = self._alert_line(model, local, bridge_live, local_live, fc_live, blackbox_status)

        border = self.color("+" + ("-" * (width - 2)) + "+", self.BORDER_CODE)
        content_width = max(1, width - 4)

        def box_row(text: str = "") -> str:
            return "| " + self._align(text, content_width) + " |"

        output = [border]

        if visible_len(title) + visible_len(meta) + 2 <= content_width:
            output.append(box_row(self._spread(title, meta, content_width)))
        else:
            for line in (title, meta):
                output.extend(box_row(wrapped) for wrapped in self._wrap_line(line, content_width))

        output.append(border)

        if content_width >= 116:
            gap = 3
            usable = content_width - (2 * gap)
            control_width = max(18, usable // 6)
            link_width = (usable - control_width) // 2
            flight_width = usable - link_width - control_width

            labels = (
                self._align(self.color("LINK / DATA", self.LABEL_CODE), link_width)
                + (" " * gap)
                + self._align(self.color("CONTROL STATE", self.LABEL_CODE), control_width, "center")
                + (" " * gap)
                + self._align(self.color("FLIGHT / POWER", self.LABEL_CODE), flight_width, "right")
            )
            badges = (
                self._align(link_badges, link_width)
                + (" " * gap)
                + self._align(control_badge, control_width, "center")
                + (" " * gap)
                + self._align(flight_badges, flight_width, "right")
            )
            output.extend((box_row(labels), box_row(badges)))
        else:
            groups = (
                self.color("LINK / DATA  ", self.LABEL_CODE) + link_badges,
                self.color("CONTROL / FLIGHT  ", self.LABEL_CODE)
                + "  ".join((control_badge, arm_badge, mode_badge, battery_badge)),
            )
            for group in groups:
                output.extend(box_row(wrapped) for wrapped in self._wrap_line(group, content_width))

        if alert:
            output.append(border)
            output.extend(box_row(wrapped) for wrapped in self._wrap_line(alert, content_width))
        output.append(border)
        return output

    def _flight_rows(self, fc: FcTelemetry) -> List[str]:
        if fc.protocol == "Never":
            return ["Waiting for the first flight-controller telemetry packet..."]
        if fc.protocol == "Original":
            return [
                "Original Bayang telemetry does not include attitude/control pages.",
                self.field("Low voltage", self.bool_text(fc.low_voltage, "ACTIVE", "clear", warn_when_true=True)),
                self.field("PID P / I / D", f"{fc.pid_p} / {fc.pid_i} / {fc.pid_d}"),
            ]
        return [
            self.field("Mode / profile", f"{fc.flight_mode} / PID {2 if fc.pid_profile else 1}"),
            self.field(
                "Armed / failsafe",
                f"{'YES' if fc.armed else 'NO'} / {self.bool_text(fc.failsafe, 'YES', 'NO', warn_when_true=True)}",
            ),
            self.field("Ground / idle-up", f"{'YES' if fc.on_ground else 'NO'} / {'YES' if fc.idle_up else 'NO'}"),
            self.field("Flight time", flight_time_text(fc.flight_time_seconds)),
            "",
            self.field("Attitude deg", f"R {fc.roll_deg:7.1f}   P {fc.pitch_deg:7.1f}   Y {fc.relative_yaw_deg:7.1f}"),
            self.field("Gyro deg/s", f"R {fc.gyro_roll_dps:7.0f}   P {fc.gyro_pitch_dps:7.0f}   Y {fc.gyro_yaw_dps:7.0f}"),
            self.field(
                "Setpoint deg/s",
                f"R {fc.setpoint_roll_dps:7.0f}   P {fc.setpoint_pitch_dps:7.0f}   Y {fc.setpoint_yaw_dps:7.0f}",
            ),
            self.field("Accel g", f"X {fc.accel_x_g:7.3f}   Y {fc.accel_y_g:7.3f}   Z {fc.accel_z_g:7.3f}"),
        ]

    def _power_rows(self, fc: FcTelemetry) -> List[str]:
        if fc.protocol == "Never":
            return ["No FC power/link telemetry received yet."]
        rows = [
            self.field("Battery raw", f"{fc.battery_raw_v:.3f} V"),
            self.field("Battery comp", f"{fc.battery_compensated_v:.3f} V"),
            self.field("Low voltage", self.bool_text(fc.low_voltage, "ACTIVE", "clear", warn_when_true=True)),
            self.field("RX packet rate", f"{fc.receiver_packets_per_second} /s"),
        ]
        if fc.protocol != "Original":
            rows.extend(
                [
                    self.field("Lost packet rate", f"{fc.packets_lost_per_second} /s"),
                    self.field("Link quality", f"{fc.link_quality_percent} %"),
                    self.field("RX gap current", f"{fc.current_rx_gap_ms:.1f} ms"),
                    self.field("RX gap maximum", f"{fc.maximum_rx_gap_ms:.1f} ms"),
                    self.field("Failsafe count", fc.failsafe_count),
                    self.field("RX total", fc.receiver_packet_total),
                    self.field("Estimated lost", fc.estimated_lost_packet_total),
                ]
            )
        return rows

    def _control_rows(self, fc: FcTelemetry, local: Optional[LocalState]) -> List[str]:
        rows: List[str] = []
        if fc.protocol == "Never":
            rows.append("FC output telemetry unavailable.")
        elif fc.protocol == "Original":
            rows.append("Motor/throttle output is unavailable in Original telemetry.")
        else:
            rows.extend(
                [
                    self.field("Throttle command", f"{fc.commanded_throttle_percent:.1f} %"),
                    self.field("Throttle applied", f"{fc.applied_throttle_percent:.1f} %"),
                    self.field("Motors M1 / M2", f"{fc.motors_percent[0]:.1f} / {fc.motors_percent[1]:.1f} %"),
                    self.field("Motors M3 / M4", f"{fc.motors_percent[2]:.1f} / {fc.motors_percent[3]:.1f} %"),
                ]
            )
        if local is not None:
            rows.extend(
                [
                    "",
                    self.field("Input roll", local.roll_raw),
                    self.field("Input pitch", local.pitch_raw),
                    self.field("Input yaw", local.yaw_raw),
                    self.field("Input throttle", local.throttle_raw),
                ]
            )
        return rows

    def _transmitter_rows(self, local: Optional[LocalState]) -> List[str]:
        if local is None:
            return ["Waiting for the first ESP32 local-state snapshot..."]
        state = SYSTEM_STATES.get(local.system_state, f"UNKNOWN({local.system_state})")
        control_enabled = bool(local.status_flags & (1 << 3))
        return [
            self.field("System state", state),
            self.field("Control", "ENABLED" if control_enabled else "SAFETY LOCKED"),
            self.field("Gamepad age", age_text(local.gamepad_age_ms)),
            self.field("FC telemetry age", age_text(local.fc_telemetry_age_ms)),
            self.field("Buttons", set_names(local.buttons, BUTTON_NAMES)),
            self.field("Aux modes", set_names(local.aux_modes, AUX_NAMES)),
            self.field("Status flags", set_names(local.status_flags, STATUS_FLAGS)),
            self.field("Radio TX packets", local.tx_packets),
            self.field("Radio TX failures", local.tx_failures),
            self.field("Consecutive fail", local.consecutive_tx_failures),
            self.field("Next hop channel", local.next_hopping_channel),
            self.field("FC accepted/reject", f"{local.telemetry_accepted} / {local.telemetry_rejected}"),
            self.field("Deadline misses", local.deadline_misses),
            self.field("Exporter drops", local.export_queue_drops),
        ]

    def _system_rows(self, fc: FcTelemetry) -> List[str]:
        if fc.protocol == "Never":
            return ["No FC system telemetry received yet."]
        if fc.protocol == "Original":
            return [
                self.field("PID P / I / D", f"{fc.pid_p} / {fc.pid_i} / {fc.pid_d}"),
                "Extended loop/IMU/CPU counters are not carried by Original telemetry.",
            ]
        return [
            self.field("Loop avg / max", f"{fc.loop_time_average_us} / {fc.loop_time_maximum_us} us"),
            self.field("Loop overruns", fc.loop_overrun_count),
            self.field("CPU load", f"{fc.cpu_load_percent} %"),
            self.field("IMU type", f"0x{fc.imu_type:02X}"),
            self.field("IMU temp raw", fc.imu_temperature_raw),
            self.field("Telemetry TX", fc.telemetry_tx_count),
            self.field("System subpages", f"0x{fc.system_subpage_mask:02X}"),
        ]

    # -----------------------------
    # Terminal 3D quad visualizer
    # -----------------------------

    @staticmethod
    def _rotate_body(
        point: Tuple[float, float, float],
        roll_deg: float,
        pitch_deg: float,
        yaw_deg: float,
    ) -> Tuple[float, float, float]:
        """Rotate body coordinates into world coordinates using FC attitude."""
        x, y, z = point
        roll = math.radians(roll_deg)
        pitch = math.radians(pitch_deg)
        yaw = math.radians(yaw_deg)

        cr, sr = math.cos(roll), math.sin(roll)
        cp, sp = math.cos(pitch), math.sin(pitch)
        cy, sy = math.cos(yaw), math.sin(yaw)

        # Body convention: +X nose/front, +Y right, +Z up.
        y, z = y * cr - z * sr, y * sr + z * cr
        x, z = x * cp + z * sp, -x * sp + z * cp
        x, y = x * cy - y * sy, x * sy + y * cy
        return x, y, z

    @staticmethod
    def _view_project(point: Tuple[float, float, float]) -> Tuple[float, float, float]:
        """Stable orthographic/isometric projection for a small terminal model.

        The screen basis is intentionally oriented so +X (the quad nose) points
        north-west at zero yaw, while +Z still projects upward on screen.
        """
        x, y, z = point

        # Fixed camera from the supplied reference renderer. The visualizer
        # applies a 180-degree display offset to yaw so zero-yaw nose remains NW.
        right = (0.8480, 0.5300, 0.0)
        up = (-0.2257, 0.3611, 0.9044)
        forward = (-0.4793, 0.7669, -0.4264)

        sx = x * right[0] + y * right[1] + z * right[2]
        sy = x * up[0] + y * up[1] + z * up[2]
        depth = x * forward[0] + y * forward[1] + z * forward[2]
        return sx, sy, depth

    def _project_quad_point(
        self,
        point: Tuple[float, float, float],
        roll: float,
        pitch: float,
        yaw: float,
        width: int,
        height: int,
        scale: float,
    ) -> Tuple[int, int, float]:
        world = self._rotate_body(point, roll, pitch, yaw)
        sx, sy, depth = self._view_project(world)
        cx = (width - 1) * 0.5
        cy = (height - 1) * 0.5
        px = int(round(cx + sx * scale))
        py = int(round(cy - sy * scale * 0.52))
        return px, py, depth

    def _visualizer_rows(self, fc: FcTelemetry, now: float, content_width: int) -> List[str]:
        """Render only the quad model; no status/attitude text is placed inside."""
        width = max(4, content_width)
        canvas_height = 9 if width < 31 else 11

        has_attitude = fc.protocol == "Extended V1" and bool(fc.page_mask & (1 << 1))
        roll = fc.roll_deg if has_attitude else 0.0
        pitch = fc.pitch_deg if has_attitude else 0.0
        yaw = (fc.relative_yaw_deg + 180.0) if has_attitude else 180.0

        motors = (
            (1.05, 1.05, 0.0),
            (1.05, -1.05, 0.0),
            (-1.05, 1.05, 0.0),
            (-1.05, -1.05, 0.0),
        )
        rotor_radius = 0.34

        if self.use_unicode:
            canvas = BrailleCanvas(width, canvas_height)
            scale = max(6.0, min(canvas.width / 3.4, canvas.height / 3.4) * 0.86)

            def p(point: Tuple[float, float, float]) -> Tuple[int, int, float]:
                world = self._rotate_body(point, roll, pitch, yaw)
                sx, sy, depth = self._view_project(world)
                px = int(round((canvas.width - 1) * 0.5 + sx * scale))
                py = int(round((canvas.height - 1) * 0.5 - sy * scale))
                return px, py, depth

            center = p((0.0, 0.0, 0.0))

            # X-frame arms.
            for motor in motors:
                target = p(motor)
                canvas.line(center[0], center[1], target[0], target[1])

            # Rotor rings live in the body's XY plane. Roll/pitch therefore turn
            # them into proper projected ellipses rather than fixed screen circles.
            rotor_steps = 32
            for mx, my, mz in motors:
                ring = [
                    p((
                        mx + rotor_radius * math.cos(2.0 * math.pi * step / rotor_steps),
                        my + rotor_radius * math.sin(2.0 * math.pi * step / rotor_steps),
                        mz,
                    ))
                    for step in range(rotor_steps)
                ]
                for index in range(rotor_steps):
                    a = ring[index]
                    b = ring[(index + 1) % rotor_steps]
                    canvas.line(a[0], a[1], b[0], b[1])
                hub = p((mx, my, mz))
                canvas.filled_dot(hub[0], hub[1], radius=1)

            # Compact 3D center body, borrowed from the reference renderer style.
            corners: List[Tuple[int, int, float]] = []
            for z in (-0.14, 0.14):
                for x, y in (
                    (0.34, 0.27),
                    (0.34, -0.27),
                    (-0.34, -0.27),
                    (-0.34, 0.27),
                ):
                    corners.append(p((x, y, z)))

            body_edges = (
                (0, 1), (1, 2), (2, 3), (3, 0),
                (4, 5), (5, 6), (6, 7), (7, 4),
                (0, 4), (1, 5), (2, 6), (3, 7),
            )
            for first, second in body_edges:
                a, b = corners[first], corners[second]
                canvas.line(a[0], a[1], b[0], b[1])

            # Physical nose mast + triangular arrow. +X is front, and the camera
            # maps +X to NW at zero yaw.
            nose_base = p((0.34, 0.0, 0.12))
            nose_tip = p((0.98, 0.0, 0.20))
            nose_left = p((0.78, 0.14, 0.16))
            nose_right = p((0.78, -0.14, 0.16))
            canvas.line(nose_base[0], nose_base[1], nose_tip[0], nose_tip[1])
            canvas.line(nose_tip[0], nose_tip[1], nose_left[0], nose_left[1])
            canvas.line(nose_tip[0], nose_tip[1], nose_right[0], nose_right[1])
            canvas.line(nose_left[0], nose_left[1], nose_right[0], nose_right[1])
            canvas.filled_dot(center[0], center[1], radius=1)

            rows = canvas.rows()
        else:
            # Safe ASCII fallback using the same 3D geometry and camera.
            canvas = TerminalCanvas(width, canvas_height)
            scale = max(3.1, min(width / 3.65, canvas_height / 1.72))

            def p(point: Tuple[float, float, float]) -> Tuple[int, int, float]:
                return self._project_quad_point(point, roll, pitch, yaw, width, canvas_height, scale)

            center = p((0.0, 0.0, 0.0))
            for motor in motors:
                target = p(motor)
                canvas.line(center[0], center[1], target[0], target[1], priority=1)

            rotor_steps = 16
            for mx, my, mz in motors:
                ring = [
                    p((
                        mx + rotor_radius * math.cos(2.0 * math.pi * step / rotor_steps),
                        my + rotor_radius * math.sin(2.0 * math.pi * step / rotor_steps),
                        mz,
                    ))
                    for step in range(rotor_steps)
                ]
                for index in range(rotor_steps):
                    a = ring[index]
                    b = ring[(index + 1) % rotor_steps]
                    canvas.line(a[0], a[1], b[0], b[1], priority=1)
                hub = p((mx, my, mz))
                canvas.put(hub[0], hub[1], "o", priority=4)

            corners = []
            for z in (-0.14, 0.14):
                for x, y in (
                    (0.34, 0.27),
                    (0.34, -0.27),
                    (-0.34, -0.27),
                    (-0.34, 0.27),
                ):
                    corners.append(p((x, y, z)))
            body_edges = (
                (0, 1), (1, 2), (2, 3), (3, 0),
                (4, 5), (5, 6), (6, 7), (7, 4),
                (0, 4), (1, 5), (2, 6), (3, 7),
            )
            for first, second in body_edges:
                a, b = corners[first], corners[second]
                canvas.line(a[0], a[1], b[0], b[1], priority=2)

            nose_base = p((0.34, 0.0, 0.12))
            nose_tip = p((0.98, 0.0, 0.20))
            nose_left = p((0.78, 0.14, 0.16))
            nose_right = p((0.78, -0.14, 0.16))
            canvas.line(nose_base[0], nose_base[1], nose_tip[0], nose_tip[1], priority=3)
            canvas.line(nose_tip[0], nose_tip[1], nose_left[0], nose_left[1], priority=3)
            canvas.line(nose_tip[0], nose_tip[1], nose_right[0], nose_right[1], priority=3)
            canvas.put(nose_tip[0], nose_tip[1], "^", priority=6)
            canvas.put(center[0], center[1], "#", priority=5)
            rows = canvas.rows()

        # Keep every canvas row so the panel height is stable while the model
        # rotates. There are intentionally no text/status rows in this panel.
        return [self.color(row, self.VALUE_CODE) if row else row for row in rows]

    def _telemetry_rows(self, model: TelemetryModel, now: float) -> List[str]:
        fc = model.fc
        pages = [PAGE_NAMES[page] for page in range(4) if fc.page_mask & (1 << page)]
        rows = [
            self.field("Protocol", fc.protocol),
            self.field("Page mask", f"0x{fc.page_mask:02X}"),
            self.field("Pages received", ", ".join(pages) if pages else "none"),
        ]
        if fc.protocol == "Extended V1":
            rows.extend(
                [
                    self.field("FC sequence", fc.sequence),
                    self.field("FC seq losses", fc.telemetry_packet_losses),
                ]
            )
        elif fc.protocol == "Original":
            rows.append(self.field("FC sequence", "n/a (Original protocol)"))
        if fc.protocol == "Extended V1":
            rows.extend(
                [
                    self.field("Page A/control age", self._page_age(fc, 0, now)),
                    self.field("Page B/flight age", self._page_age(fc, 1, now)),
                    self.field("Page C/power age", self._page_age(fc, 2, now)),
                    self.field("Page D/system age", self._page_age(fc, 3, now)),
                ]
            )
        rows.extend(
            [
                "",
                self.field("Frames valid", model.stats.valid_frames),
                self.field("Frames malformed", model.stats.malformed_frames),
                self.field("Bayang invalid", model.stats.invalid_bayang_packets),
                self.field("Bridge gaps FC", model.stats.type1_sequence_gaps),
                self.field("Bridge gaps local", model.stats.type2_sequence_gaps),
            ]
        )
        return rows

    def render(
        self,
        model: TelemetryModel,
        port: str,
        now: Optional[float] = None,
        width: Optional[int] = None,
        blackbox_status: Optional[Dict[str, object]] = None,
    ) -> str:
        current = time.monotonic() if now is None else now
        bridge_live = model.last_any_at is not None and current - model.last_any_at <= 0.5
        local_live = model.last_type2_at is not None and current - model.last_type2_at <= 0.25
        fc_live = model.last_type1_at is not None and current - model.last_type1_at <= 0.5
        local = model.local
        fc = model.fc

        width = self._terminal_width() if width is None else max(8, width)
        lines = self._header(model, port, current, width, bridge_live, local_live, fc_live, blackbox_status)

        gap = 2
        if width >= 96:
            usable = width - (2 * gap)
            base = usable // 3
            widths = [base, base, usable - (2 * base)]

            flight_panel = self.panel("PRIMARY FLIGHT", self._flight_rows(fc), widths[0])
            power_panel = self.panel("POWER / RF LINK", self._power_rows(fc), widths[1])
            control_panel = self.panel("CONTROL / OUTPUT", self._control_rows(fc, local), widths[2])

            # Keep each horizontal band rectangular: shorter regular panels grow
            # downward so unused space remains inside their borders.
            top_height = max(len(flight_panel), len(power_panel), len(control_panel))
            flight_panel = self._grow_panel(flight_panel, widths[0], top_height)
            power_panel = self._grow_panel(power_panel, widths[1], top_height)
            control_panel = self._grow_panel(control_panel, widths[2], top_height)

            transmitter_panel = self.panel("TRANSMITTER / INPUT", self._transmitter_rows(local), widths[0])
            system_panel = self.panel("FC SYSTEM", self._system_rows(fc), widths[1])
            visualizer_panel = self.panel(
                "3D QUAD / ATTITUDE",
                self._visualizer_rows(fc, current, widths[1] - 4),
                widths[1],
            )
            # FC SYSTEM and the quad keep their natural heights. There is exactly
            # one blank terminal row between them. The neighboring regular boxes
            # expand to match the full stack height instead of leaving whitespace
            # outside their borders.
            system_stack = system_panel + [(" " * widths[1])] + visualizer_panel
            telemetry_panel = self.panel("TELEMETRY / PROTOCOL", self._telemetry_rows(model, current), widths[2])

            stack_height = len(system_stack)
            transmitter_panel = self._grow_panel(transmitter_panel, widths[0], stack_height)
            telemetry_panel = self._grow_panel(telemetry_panel, widths[2], stack_height)

            lines.extend(self._join_many_columns([flight_panel, power_panel, control_panel], widths, gap))
            lines.extend(self._join_many_columns([transmitter_panel, system_stack, telemetry_panel], widths, gap))

        elif width >= 72:
            lines.append("")
            left_width = (width - gap) // 2
            right_width = width - gap - left_width
            flight_panel = self.panel("PRIMARY FLIGHT", self._flight_rows(fc), left_width)
            power_panel = self.panel("POWER / RF LINK", self._power_rows(fc), right_width)
            first_height = max(len(flight_panel), len(power_panel))
            flight_panel = self._grow_panel(flight_panel, left_width, first_height)
            power_panel = self._grow_panel(power_panel, right_width, first_height)

            control_panel = self.panel("CONTROL / OUTPUT", self._control_rows(fc, local), left_width)
            transmitter_panel = self.panel("TRANSMITTER / INPUT", self._transmitter_rows(local), right_width)
            second_height = max(len(control_panel), len(transmitter_panel))
            control_panel = self._grow_panel(control_panel, left_width, second_height)
            transmitter_panel = self._grow_panel(transmitter_panel, right_width, second_height)

            system_stack = (
                self.panel("FC SYSTEM", self._system_rows(fc), left_width)
                + [(" " * left_width)]
                + self.panel("3D QUAD / ATTITUDE", self._visualizer_rows(fc, current, left_width - 4), left_width)
            )
            telemetry_panel = self.panel("TELEMETRY / PROTOCOL", self._telemetry_rows(model, current), right_width)
            telemetry_panel = self._grow_panel(telemetry_panel, right_width, len(system_stack))

            pairs = [
                (flight_panel, power_panel),
                (control_panel, transmitter_panel),
                (system_stack, telemetry_panel),
            ]
            for pair_index, (left, right) in enumerate(pairs):
                lines.extend(self._join_columns(left, right, left_width, right_width, gap))
                if pair_index != len(pairs) - 1:
                    lines.append("")
        else:
            lines.append("")
            panels = [
                ("PRIMARY FLIGHT", self._flight_rows(fc)),
                ("POWER / RF LINK", self._power_rows(fc)),
                ("CONTROL / OUTPUT", self._control_rows(fc, local)),
                ("TRANSMITTER / INPUT", self._transmitter_rows(local)),
                ("FC SYSTEM", self._system_rows(fc)),
                ("3D QUAD / ATTITUDE", self._visualizer_rows(fc, current, width - 4)),
                ("TELEMETRY / PROTOCOL", self._telemetry_rows(model, current)),
            ]
            for index, (title, rows) in enumerate(panels):
                lines.extend(self.panel(title, rows, width))
                if index != len(panels) - 1:
                    lines.append("")

        return "\n".join(lines)


@dataclasses.dataclass(frozen=True)
class ScreenSize:
    """Drawable terminal area, excluding the wrap-prone final column."""

    width: int
    height: int


class TerminalRenderer:
    """Update a terminal dashboard without redrawing unchanged rows."""

    ENTER_SCREEN = "\033[?1049h\033[?25l\033[?7l\033[2J\033[H"
    LEAVE_SCREEN = "\033[0m\033[?7h\033[?25h\033[?1049l"

    def __init__(self, stream: TextIO, interactive: bool):
        self.stream = stream
        self.interactive = interactive
        self._previous_lines: List[str] = []
        self._previous_size: Optional[ScreenSize] = None

    def measure(self) -> ScreenSize:
        try:
            terminal = shutil.get_terminal_size(fallback=(120, 40))
            return ScreenSize(max(8, terminal.columns - 1), max(1, terminal.lines))
        except OSError:
            return ScreenSize(119, 40)

    def start(self) -> None:
        if not self.interactive:
            return
        self.stream.write(self.ENTER_SCREEN)
        self.stream.flush()

    def draw(self, frame: str, size: ScreenSize) -> None:
        if not self.interactive:
            self.stream.write(frame + "\n\n")
            self.stream.flush()
            return

        lines = frame.splitlines()
        if len(lines) > size.height:
            lines = lines[:size.height]
            notice = "[dashboard clipped: enlarge the terminal to view all panels]"
            lines[-1] = notice[:size.width]

        resized = size != self._previous_size
        previous = [] if resized else self._previous_lines
        commands: List[str] = ["\033[2J\033[H"] if resized else []

        # Erasing the complete row before writing fixes stale suffixes when a
        # badge or value becomes shorter. It also clears rows from a frame that
        # has become shorter after a protocol or terminal-layout change.
        row_count = max(len(lines), len(previous))
        for index in range(row_count):
            current = lines[index] if index < len(lines) else ""
            old = previous[index] if index < len(previous) else None
            if current == old:
                continue
            commands.append(f"\033[{index + 1};1H\033[0m\033[2K{current}")

        if commands:
            commands.append("\033[H")
            self.stream.write("".join(commands))
            self.stream.flush()

        self._previous_lines = lines
        self._previous_size = size

    def stop(self) -> None:
        if not self.interactive:
            return
        self.stream.write(self.LEAVE_SCREEN)
        self.stream.flush()


def likely_ports() -> List[object]:
    try:
        from serial.tools import list_ports
    except ImportError as exc:
        raise RuntimeError("pyserial is required; install it with: python3 -m pip install pyserial") from exc

    ports = list(list_ports.comports())

    def score(port: object) -> Tuple[int, str]:
        device = str(getattr(port, "device", ""))
        description = str(getattr(port, "description", "")).lower()
        hardware_id = str(getattr(port, "hwid", "")).lower()
        preferred = any(token in device.lower() for token in ("ttyusb", "ttyacm", "cu.usb", "com"))
        esp = "esp32" in description or "10c4:" in hardware_id or "1a86:" in hardware_id
        return (0 if esp else 1 if preferred else 2, device)

    return sorted(ports, key=score)


def serial_candidates(requested_port: Optional[str], detected_ports: Iterable[object]) -> List[str]:
    """Return unique candidates, retaining auto-detected re-enumerated ports."""
    devices: List[str] = []
    for port in detected_ports:
        device = str(getattr(port, "device", ""))
        description = str(getattr(port, "description", "")).lower()
        hardware_id = str(getattr(port, "hwid", "")).lower()
        device_lower = device.lower()
        usb_style = any(token in device_lower for token in ("ttyusb", "ttyacm", "cu.usb", "com"))
        known_adapter = "esp32" in description or "10c4:" in hardware_id or "1a86:" in hardware_id
        if device and (usb_style or known_adapter):
            devices.append(device)
    choices = ([requested_port] if requested_port else []) + devices
    return list(dict.fromkeys(choices))


def open_serial_port(serial_module: object, device: str, baud: int) -> object:
    """Open a port without asserting the ESP32 auto-reset control lines."""
    connection = serial_module.Serial()
    try:
        connection.port = device
        connection.baudrate = baud
        connection.timeout = SERIAL_READ_TIMEOUT_S
        connection.write_timeout = SERIAL_READ_TIMEOUT_S

        # Set these before open(). PySerial otherwise defaults DTR high, which
        # can pulse EN/BOOT through the common ESP32 USB auto-reset circuit.
        connection.dtr = False
        connection.rts = False
        connection.open()
        return connection
    except Exception:
        try:
            connection.close()
        except Exception:
            pass
        raise


def serial_connection_stale(connection: object, last_byte_at: float, now: float) -> bool:
    """Detect dead USB handles that remain open after device re-enumeration."""
    return not bool(getattr(connection, "is_open", True)) or now - last_byte_at >= SERIAL_SILENCE_TIMEOUT_S


def close_serial_port(connection: object) -> None:
    try:
        connection.close()
    except Exception:
        pass


def print_ports() -> int:
    ports = likely_ports()
    if not ports:
        print("No serial ports found.")
        return 1
    for port in ports:
        print(f"{port.device:20} {port.description} [{port.hwid}]")
    return 0


def run_viewer(
    requested_port: Optional[str],
    baud: int,
    refresh_hz: float,
    use_color: bool,
    use_ansi: bool,
    logs_dir: Path,
    max_log_storage: int,
    no_blackbox: bool,
) -> int:
    try:
        import serial
    except ImportError:
        print("Missing dependency: pyserial", file=sys.stderr)
        print("Install it with: python3 -m pip install pyserial", file=sys.stderr)
        return 2

    model = TelemetryModel()
    dashboard = Dashboard(use_color)
    renderer = TerminalRenderer(sys.stdout, use_ansi)
    if no_blackbox:
        recorder: object = DisabledBlackbox()
    else:
        try:
            recorder = BlackboxRecorder(
                BlackboxConfig(logs_dir=logs_dir, max_storage_bytes=max_log_storage),
                requested_port,
                baud,
                sys.argv,
            )
        except OSError as exc:
            recorder = DisabledBlackbox(f"could not start blackbox: {exc}")
    event_tracker = FlightEventTracker()
    buffer = bytearray()
    serial_port = None
    active_port = requested_port or "auto-detecting"
    connection_id = 0
    last_render = 0.0
    retry_at = 0.0
    last_serial_byte_at = 0.0
    last_open_failure_event_at = -10.0
    next_sample_at = 0.0
    malformed_since_event = 0
    last_malformed_event_at = -10.0
    last_malformed_error = ""
    exit_reason = "unexpected shutdown"
    # A redirected stream cannot update rows in place. Limit plain snapshots so
    # logging or an ANSI-incompatible terminal is not flooded with full frames.
    render_period = 1.0 / refresh_hz if use_ansi else max(1.0, 1.0 / refresh_hz)

    renderer.start()

    try:
        while True:
            now = time.monotonic()

            if serial_port is None and now >= retry_at:
                choices = serial_candidates(requested_port, likely_ports())

                if not choices:
                    active_port = "auto-detecting"
                else:
                    for candidate in choices:
                        active_port = candidate
                        try:
                            serial_port = open_serial_port(serial, candidate, baud)
                            last_serial_byte_at = time.monotonic()
                            connection_id += 1
                            recorder.connection(connection_id, candidate, "connected")
                            buffer.clear()
                            break
                        except (OSError, ValueError, serial.SerialException) as exc:
                            serial_port = None
                            if now - last_open_failure_event_at >= 5.0:
                                recorder.event(
                                    "serial_open_failed",
                                    "warning",
                                    f"Could not open {candidate}",
                                    {"port": candidate, "error": str(exc)},
                                )
                                last_open_failure_event_at = now
                    if serial_port is None:
                        active_port = f"{requested_port} (reconnecting)" if requested_port else "auto-detecting"

                retry_at = now + SERIAL_RETRY_DELAY_S

            if serial_port is not None:
                try:
                    chunk = serial_port.read(512)
                    if chunk:
                        last_serial_byte_at = time.monotonic()
                        recorder.raw(chunk, connection_id)
                        buffer.extend(chunk)

                    if b"\x00" in buffer:
                        segments = buffer.split(b"\x00")
                        buffer[:] = segments.pop()
                    else:
                        segments = []

                    for encoded in segments:
                        if not encoded:
                            continue
                        try:
                            update = model.accept(decode_bridge_frame(bytes(encoded)))
                            event_tracker.accept(recorder, model, update)
                        except DecodeError as exc:
                            model.stats.malformed_frames += 1
                            malformed_since_event += 1
                            last_malformed_error = str(exc)
                            event_now = time.monotonic()
                            if event_now - last_malformed_event_at >= 5.0:
                                recorder.event(
                                    "malformed_frames",
                                    "warning",
                                    f"Discarded {malformed_since_event} malformed telemetry frame(s)",
                                    {"count": malformed_since_event, "last_error": last_malformed_error},
                                )
                                malformed_since_event = 0
                                last_malformed_event_at = event_now

                    if len(buffer) > 4096:
                        buffer.clear()
                        model.stats.malformed_frames += 1
                        recorder.event(
                            "serial_buffer_overflow",
                            "warning",
                            "Serial synchronization buffer was cleared",
                            {},
                        )

                    now = time.monotonic()
                    if serial_connection_stale(serial_port, last_serial_byte_at, now):
                        stale_port = active_port
                        recorder.connection(connection_id, stale_port, "disconnected")
                        close_serial_port(serial_port)
                        serial_port = None
                        buffer.clear()
                        active_port = f"{stale_port} (reconnecting)"
                        retry_at = now + SERIAL_RETRY_DELAY_S

                except (OSError, serial.SerialException) as exc:
                    recorder.event(
                        "serial_read_failed",
                        "warning",
                        f"Serial read failed on {active_port}",
                        {"port": active_port, "error": str(exc)},
                    )
                    recorder.connection(connection_id, active_port, "disconnected")
                    close_serial_port(serial_port)
                    serial_port = None
                    buffer.clear()
                    active_port = f"{active_port} (reconnecting)"
                    retry_at = time.monotonic() + SERIAL_RETRY_DELAY_S

            now = time.monotonic()
            event_tracker.health(recorder, model, now)
            sample_period = blackbox_sample_period(model)
            if sample_period is not None and now >= next_sample_at:
                recorder.sample(telemetry_snapshot(model, active_port, connection_id, now))
                next_sample_at = now + sample_period

            if now - last_render >= render_period:
                screen = renderer.measure()
                output = dashboard.render(model, active_port, now, screen.width, recorder.status())
                renderer.draw(output, screen)
                last_render = now

            if serial_port is None:
                time.sleep(0.05)

    except KeyboardInterrupt:
        exit_reason = "Ctrl+C"
        return 0
    finally:
        if serial_port is not None:
            recorder.connection(connection_id, active_port, "disconnected")
            close_serial_port(serial_port)
        if malformed_since_event:
            recorder.event(
                "malformed_frames",
                "warning",
                f"Discarded {malformed_since_event} malformed telemetry frame(s)",
                {"count": malformed_since_event, "last_error": last_malformed_error},
            )
        recorder_status = recorder.status()
        recorder.close(exit_reason)
        renderer.stop()
        if recorder_status.get("enabled") and recorder_status.get("session_dir"):
            print(f"Blackbox saved: {recorder_status['session_dir']}")


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", nargs="?", help="serial port; omitted to auto-detect the ESP32")
    parser.add_argument("--baud", type=int, default=115200, help="serial baud rate (default: 115200)")
    parser.add_argument("--refresh", type=float, default=10.0, help="dashboard refresh rate in Hz (default: 10)")
    parser.add_argument("--list", action="store_true", help="list available serial ports and exit")
    parser.add_argument(
        "--inspect-log",
        type=Path,
        metavar="PATH",
        help="validate and summarize a session directory, or the newest session in a logs directory",
    )
    parser.add_argument("--inspect-json", action="store_true", help="emit --inspect-log results as JSON")
    parser.add_argument("--no-color", action="store_true", help="disable dashboard colors")
    parser.add_argument(
        "--logs-dir",
        type=Path,
        default=Path(__file__).resolve().parent / "logs",
        help="blackbox session directory (default: tool logs directory)",
    )
    parser.add_argument(
        "--max-log-storage",
        default="2G",
        metavar="SIZE",
        help="maximum blackbox storage, such as 512M or 2G; 0 disables pruning",
    )
    parser.add_argument("--no-blackbox", action="store_true", help="disable blackbox recording")
    args = parser.parse_args(argv)
    if args.refresh <= 0:
        parser.error("--refresh must be greater than zero")
    if args.refresh > 60:
        parser.error("--refresh must not exceed 60 Hz")
    if args.baud <= 0:
        parser.error("--baud must be greater than zero")
    if args.inspect_json and args.inspect_log is None:
        parser.error("--inspect-json requires --inspect-log")
    try:
        args.max_log_storage = parse_storage_size(args.max_log_storage)
    except ValueError as exc:
        parser.error(str(exc))
    return args


def interactive_ansi_available() -> bool:
    if not sys.stdout.isatty() or os.environ.get("TERM", "").lower() == "dumb":
        return False

    if os.name != "nt":
        return True

    try:
        import ctypes

        kernel32 = ctypes.windll.kernel32
        kernel32.GetStdHandle.restype = ctypes.c_void_p
        kernel32.GetConsoleMode.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32)]
        kernel32.SetConsoleMode.argtypes = [ctypes.c_void_p, ctypes.c_uint32]

        handle = kernel32.GetStdHandle(-11)
        invalid_handle = ctypes.c_void_p(-1).value
        mode = ctypes.c_uint32()
        if handle in (None, invalid_handle) or not kernel32.GetConsoleMode(handle, ctypes.byref(mode)):
            return False
        return bool(kernel32.SetConsoleMode(handle, mode.value | 0x0004))
    except Exception:
        return False


def main(argv: Optional[List[str]] = None) -> int:
    args = parse_args(argv)
    if args.inspect_log is not None:
        try:
            return print_session_inspection(inspect_session(args.inspect_log), args.inspect_json)
        except ValueError as exc:
            print(f"Could not inspect blackbox: {exc}", file=sys.stderr)
            return 2
    if args.list:
        try:
            return print_ports()
        except RuntimeError as exc:
            print(exc, file=sys.stderr)
            return 2
    use_ansi = interactive_ansi_available()
    use_color = not args.no_color and use_ansi
    return run_viewer(
        args.port,
        args.baud,
        args.refresh,
        use_color,
        use_ansi,
        args.logs_dir.resolve(),
        args.max_log_storage,
        args.no_blackbox,
    )


if __name__ == "__main__":
    raise SystemExit(main())
