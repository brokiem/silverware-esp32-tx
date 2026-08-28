"""Rotating, crash-tolerant blackbox recorder for the Silverware ground station."""

from __future__ import annotations

import csv
import dataclasses
import errno
import gzip
import json
import math
import os
import platform
import queue
import shutil
import struct
import sys
import threading
import time
import zlib
from collections import Counter, deque
from contextlib import contextmanager
from datetime import datetime, timezone
from pathlib import Path
from typing import BinaryIO, Deque, Dict, Iterable, Iterator, List, Mapping, Optional, Tuple


SBB_MAGIC = b"SWTXSBB1"
SBB_HEADER = struct.Struct(">8sQ")
SBB_RECORD_HEADER = struct.Struct(">BQQIH")
SBB_RECORD_CRC = struct.Struct(">I")
SBB_RECORD_SERIAL = 1
SBB_MAX_PAYLOAD = (1 << 16) - 1


class BlackboxFormatError(ValueError):
    """A blackbox file is truncated or fails an integrity check."""


@dataclasses.dataclass(frozen=True)
class BlackboxConfig:
    logs_dir: Path
    max_storage_bytes: int = 2 * 1024**3
    target_storage_ratio: float = 0.90
    rotate_bytes: int = 16 * 1024**2
    rotate_seconds: float = 15 * 60.0
    queue_size: int = 8192
    flush_seconds: float = 1.0
    fsync_seconds: float = 5.0
    gzip_level: int = 3


@dataclasses.dataclass(frozen=True)
class SbbRecord:
    elapsed_ns: int
    utc_ns: int
    connection_id: int
    data: bytes


def utc_text(utc_ns: Optional[int] = None) -> str:
    timestamp = time.time_ns() if utc_ns is None else utc_ns
    return datetime.fromtimestamp(timestamp / 1_000_000_000, timezone.utc).isoformat(timespec="milliseconds")


def parse_storage_size(text: str) -> int:
    value = text.strip().upper()
    if value == "0":
        return 0
    units = {"K": 1024, "M": 1024**2, "G": 1024**3, "T": 1024**4}
    suffix = value[-1:]
    multiplier = units.get(suffix, 1)
    number = value[:-1] if suffix in units else value
    try:
        parsed = float(number)
    except ValueError as exc:
        raise ValueError(f"invalid storage size: {text}") from exc
    if not math.isfinite(parsed):
        raise ValueError("storage size must be finite")
    if parsed < 0:
        raise ValueError("storage size must not be negative")
    return int(parsed * multiplier)


def directory_size(path: Path) -> int:
    total = 0
    try:
        for item in path.rglob("*"):
            try:
                if item.is_file():
                    total += item.stat().st_size
            except OSError:
                continue
    except OSError:
        pass
    return total


def _flat_directory_size(path: Path) -> int:
    total = 0
    try:
        for item in path.iterdir():
            try:
                if item.is_file():
                    total += item.stat().st_size
            except OSError:
                continue
    except OSError:
        pass
    return total


class _AdvisoryFileLock:
    """Small cross-platform advisory lock held for the lifetime of its stream."""

    def __init__(self, path: Path) -> None:
        self.path = path
        self.stream: Optional[BinaryIO] = None

    def acquire(self, blocking: bool) -> bool:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        stream = self.path.open("a+b")
        try:
            if os.name == "nt":
                import msvcrt

                stream.seek(0, os.SEEK_END)
                if stream.tell() == 0:
                    stream.write(b"\0")
                    stream.flush()
                stream.seek(0)
                mode = msvcrt.LK_LOCK if blocking else msvcrt.LK_NBLCK
                msvcrt.locking(stream.fileno(), mode, 1)
            else:
                import fcntl

                operation = fcntl.LOCK_EX
                if not blocking:
                    operation |= fcntl.LOCK_NB
                fcntl.flock(stream.fileno(), operation)
        except (BlockingIOError, OSError):
            stream.close()
            if blocking:
                raise
            return False
        self.stream = stream
        return True

    def release(self) -> None:
        stream, self.stream = self.stream, None
        if stream is None:
            return
        try:
            if os.name == "nt":
                import msvcrt

                stream.seek(0)
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                import fcntl

                fcntl.flock(stream.fileno(), fcntl.LOCK_UN)
        finally:
            stream.close()


@contextmanager
def _exclusive_lock(path: Path, blocking: bool = True) -> Iterator[Optional[_AdvisoryFileLock]]:
    lock = _AdvisoryFileLock(path)
    acquired = lock.acquire(blocking)
    try:
        yield lock if acquired else None
    finally:
        if acquired:
            lock.release()


def iter_sbb_records(path: Path) -> Iterator[SbbRecord]:
    opener = gzip.open if path.suffix == ".gz" else open
    with opener(path, "rb") as stream:
        header = stream.read(SBB_HEADER.size)
        if len(header) != SBB_HEADER.size:
            raise BlackboxFormatError("truncated SBB header")
        magic, _start_utc_ns = SBB_HEADER.unpack(header)
        if magic != SBB_MAGIC:
            raise BlackboxFormatError("unknown SBB format")

        while True:
            record_header = stream.read(SBB_RECORD_HEADER.size)
            if not record_header:
                return
            if len(record_header) != SBB_RECORD_HEADER.size:
                raise BlackboxFormatError("truncated SBB record header")
            kind, elapsed_ns, utc_ns, connection_id, length = SBB_RECORD_HEADER.unpack(record_header)
            if kind != SBB_RECORD_SERIAL:
                raise BlackboxFormatError(f"unknown SBB record kind {kind}")
            data = stream.read(length)
            crc_bytes = stream.read(SBB_RECORD_CRC.size)
            if len(data) != length or len(crc_bytes) != SBB_RECORD_CRC.size:
                raise BlackboxFormatError("truncated SBB record")
            expected_crc = SBB_RECORD_CRC.unpack(crc_bytes)[0]
            actual_crc = zlib.crc32(record_header + data) & 0xFFFFFFFF
            if actual_crc != expected_crc:
                raise BlackboxFormatError("SBB record CRC mismatch")
            yield SbbRecord(elapsed_ns, utc_ns, connection_id, data)


def _float_or_none(value: object) -> Optional[float]:
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def inspect_session(path: Path) -> Dict[str, object]:
    """Validate and summarize one session, or the newest session below a logs directory."""

    requested = path.expanduser().resolve()
    if (requested / "session.json").is_file():
        session_dir = requested
    else:
        sessions = _session_directories(requested)
        if not sessions:
            raise ValueError(f"no blackbox sessions found in {requested}")
        session_dir = sessions[-1][1]

    try:
        metadata = json.loads((session_dir / "session.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"cannot read session metadata: {exc}") from exc

    ownership = _AdvisoryFileLock(session_dir / ".active.lock")
    is_live = not ownership.acquire(blocking=False)
    if not is_live:
        ownership.release()

    integrity_errors: List[str] = []
    raw_records = 0
    raw_bytes = 0
    connection_ids = set()
    first_raw_utc_ns: Optional[int] = None
    last_raw_utc_ns: Optional[int] = None
    raw_files = sorted(session_dir.glob("serial-*.sbb")) + sorted(session_dir.glob("serial-*.sbb.gz"))
    for raw_path in raw_files:
        try:
            for record in iter_sbb_records(raw_path):
                raw_records += 1
                raw_bytes += len(record.data)
                connection_ids.add(record.connection_id)
                first_raw_utc_ns = record.utc_ns if first_raw_utc_ns is None else min(first_raw_utc_ns, record.utc_ns)
                last_raw_utc_ns = record.utc_ns if last_raw_utc_ns is None else max(last_raw_utc_ns, record.utc_ns)
        except (OSError, BlackboxFormatError) as exc:
            integrity_errors.append(f"{raw_path.name}: {exc}")

    event_counts: Counter[str] = Counter()
    severity_counts: Counter[str] = Counter()
    noteworthy: Deque[Dict[str, object]] = deque(maxlen=20)
    events_path = session_dir / "events.jsonl"
    try:
        with events_path.open("r", encoding="utf-8") as stream:
            for line_number, line in enumerate(stream, 1):
                if not line.strip():
                    continue
                try:
                    event = json.loads(line)
                except json.JSONDecodeError as exc:
                    integrity_errors.append(f"events.jsonl:{line_number}: {exc.msg}")
                    continue
                event_type = str(event.get("type", "unknown"))
                severity = str(event.get("severity", "unknown"))
                event_counts[event_type] += 1
                severity_counts[severity] += 1
                if severity in ("warning", "error", "critical"):
                    noteworthy.append(
                        {
                            "utc": event.get("utc"),
                            "type": event_type,
                            "message": event.get("message", ""),
                        }
                    )
    except OSError as exc:
        integrity_errors.append(f"events.jsonl: {exc}")

    csv_rows = 0
    minimum_battery_v: Optional[float] = None
    maximum_rx_gap_ms: Optional[float] = None
    armed_samples = 0
    modes = set()
    csv_files = sorted(session_dir.glob("telemetry-*.csv")) + sorted(session_dir.glob("telemetry-*.csv.gz"))
    for csv_path in csv_files:
        opener = gzip.open if csv_path.suffix == ".gz" else open
        try:
            with opener(csv_path, "rt", encoding="utf-8", newline="") as stream:
                for row in csv.DictReader(stream):
                    csv_rows += 1
                    battery = _float_or_none(row.get("fc_battery_compensated_v"))
                    if battery is not None and battery > 0:
                        minimum_battery_v = battery if minimum_battery_v is None else min(minimum_battery_v, battery)
                    gap = _float_or_none(row.get("fc_maximum_rx_gap_ms"))
                    if gap is None:
                        gap = _float_or_none(row.get("fc_current_rx_gap_ms"))
                    if gap is not None:
                        maximum_rx_gap_ms = gap if maximum_rx_gap_ms is None else max(maximum_rx_gap_ms, gap)
                    if str(row.get("fc_armed", "")).lower() in ("true", "1"):
                        armed_samples += 1
                    mode = str(row.get("fc_flight_mode", "")).strip()
                    if mode and mode not in ("--", "Unknown"):
                        modes.add(mode)
        except (OSError, csv.Error, UnicodeError) as exc:
            integrity_errors.append(f"{csv_path.name}: {exc}")

    return {
        "session_dir": str(session_dir),
        "state": metadata.get("state", "unknown"),
        "live": is_live,
        "start_utc": metadata.get("start_utc"),
        "end_utc": metadata.get("end_utc"),
        "duration_s": metadata.get("duration_s"),
        "exit_reason": metadata.get("exit_reason"),
        "requested_port": metadata.get("requested_port"),
        "disk_bytes": directory_size(session_dir),
        "raw_files": len(raw_files),
        "raw_records": raw_records,
        "raw_bytes": raw_bytes,
        "first_raw_utc": utc_text(first_raw_utc_ns) if first_raw_utc_ns is not None else None,
        "last_raw_utc": utc_text(last_raw_utc_ns) if last_raw_utc_ns is not None else None,
        "connection_ids": sorted(connection_ids),
        "csv_files": len(csv_files),
        "csv_rows": csv_rows,
        "armed_samples": armed_samples,
        "minimum_battery_v": minimum_battery_v,
        "maximum_rx_gap_ms": maximum_rx_gap_ms,
        "modes": sorted(modes),
        "event_counts": dict(sorted(event_counts.items())),
        "severity_counts": dict(sorted(severity_counts.items())),
        "recent_warnings": list(noteworthy),
        "integrity_errors": integrity_errors,
        "metadata_errors": metadata.get("errors", []),
        "pruned_ranges": metadata.get("pruned_ranges", []),
    }


def _atomic_json(path: Path, value: Mapping[str, object]) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("w", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def _session_directories(logs_dir: Path) -> List[Tuple[int, Path, Dict[str, object]]]:
    sessions: List[Tuple[int, Path, Dict[str, object]]] = []
    try:
        children = list(logs_dir.iterdir())
    except OSError:
        return sessions
    for child in children:
        metadata_path = child / "session.json"
        if not child.is_dir() or not metadata_path.is_file():
            continue
        try:
            metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
            start_ns = int(metadata.get("start_utc_ns", 0))
            sessions.append((start_ns, child, metadata))
        except (OSError, ValueError, TypeError, json.JSONDecodeError):
            continue
    return sorted(sessions, key=lambda item: item[0])


def _recover_interrupted_sessions_locked(logs_dir: Path) -> None:
    for _start, path, metadata in _session_directories(logs_dir):
        if metadata.get("state") != "active":
            continue
        session_lock = _AdvisoryFileLock(path / ".active.lock")
        if not session_lock.acquire(blocking=False):
            # Another ground-station process still owns this session.
            continue
        session_lock.release()
        for temporary in path.glob("*.gz.tmp"):
            try:
                temporary.unlink()
            except OSError:
                pass
        for chunk in metadata.get("chunks", []):
            if isinstance(chunk, dict) and chunk.get("state") == "compressing":
                chunk["state"] = "uncompressed_after_interruption"
        metadata["state"] = "interrupted"
        metadata["end_utc_ns"] = time.time_ns()
        metadata["end_utc"] = utc_text(int(metadata["end_utc_ns"]))
        metadata["exit_reason"] = "previous process ended without closing the recorder"
        try:
            _atomic_json(path / "session.json", metadata)
        except OSError:
            continue


def recover_interrupted_sessions(logs_dir: Path) -> None:
    """Recover abandoned sessions without touching sessions owned by live processes."""

    logs_dir.mkdir(parents=True, exist_ok=True)
    with _exclusive_lock(logs_dir / ".quota.lock"):
        _recover_interrupted_sessions_locked(logs_dir)


class BlackboxRecorder:
    """Non-blocking session recorder with rotating and compressed chunks."""

    def __init__(
        self,
        config: BlackboxConfig,
        requested_port: Optional[str],
        baud: int,
        argv: Iterable[str],
    ) -> None:
        self.config = config
        self.requested_port = requested_port
        self.baud = baud
        self.start_utc_ns = time.time_ns()
        self.start_monotonic_ns = time.monotonic_ns()
        self._queue: queue.Queue[Tuple[str, object]] = queue.Queue(maxsize=config.queue_size)
        self._compress_queue: queue.Queue[Optional[Tuple[Path, int]]] = queue.Queue()
        self._lock = threading.RLock()
        self._stop_requested = False
        self._healthy = True
        self._error = ""
        self._dropped_items = 0
        self._queue_overflow_reported = False
        self._bytes_written = 0
        self._session_usage = 0
        self._total_usage = 0
        self._connections: List[Dict[str, object]] = []
        self._chunks: List[Dict[str, object]] = []
        self._pruned_ranges: List[Dict[str, object]] = []
        self._errors: List[str] = []
        self._event_count = 0
        self._sample_count = 0
        self._raw_record_count = 0
        self._csv_fields: Optional[List[str]] = None
        self._raw_index = 0
        self._csv_index = 0
        self._raw_stream = None
        self._csv_stream = None
        self._csv_writer = None
        self._raw_path: Optional[Path] = None
        self._csv_path: Optional[Path] = None
        self._raw_open_elapsed = 0.0
        self._csv_open_elapsed = 0.0
        self._writer_done = threading.Event()
        self._compressor_done = threading.Event()
        self._closed = False
        self._session_lock: Optional[_AdvisoryFileLock] = None

        config.logs_dir.mkdir(parents=True, exist_ok=True)
        with _exclusive_lock(config.logs_dir / ".quota.lock"):
            _recover_interrupted_sessions_locked(config.logs_dir)
            self._startup_pruned_sessions = self._prune_old_sessions_locked(exclude=None)
        self.session_dir = self._create_session_dir(config.logs_dir)
        self._session_lock = _AdvisoryFileLock(self.session_dir / ".active.lock")
        self._session_lock.acquire(blocking=True)
        self.metadata_path = self.session_dir / "session.json"
        self.events_path = self.session_dir / "events.jsonl"
        self._events_stream = self.events_path.open("a", encoding="utf-8", buffering=1)
        self._metadata: Dict[str, object] = {
            "schema_version": 1,
            "state": "active",
            "start_utc_ns": self.start_utc_ns,
            "start_utc": utc_text(self.start_utc_ns),
            "process_id": os.getpid(),
            "end_utc_ns": None,
            "end_utc": None,
            "exit_reason": None,
            "requested_port": requested_port,
            "baud": baud,
            "argv": list(argv),
            "python": sys.version,
            "platform": platform.platform(),
            "hostname": platform.node(),
            "max_storage_bytes": config.max_storage_bytes,
            "rotate_bytes": config.rotate_bytes,
            "rotate_seconds": config.rotate_seconds,
            "connections": self._connections,
            "chunks": self._chunks,
            "pruned_ranges": self._pruned_ranges,
            "pruned_sessions": list(self._startup_pruned_sessions),
            "errors": self._errors,
        }
        _atomic_json(self.metadata_path, self._metadata_snapshot())
        self._session_usage = _flat_directory_size(self.session_dir)
        self._total_usage = directory_size(config.logs_dir)

        self._compressor = threading.Thread(target=self._compress_loop, name="blackbox-compressor", daemon=True)
        self._writer = threading.Thread(target=self._writer_loop, name="blackbox-writer", daemon=True)
        self._compressor.start()
        self._writer.start()
        self.event("session_start", "info", "Blackbox session started", {"session": self.session_dir.name})
        if self._startup_pruned_sessions:
            self.event(
                "storage_pruned",
                "info",
                "Removed old blackbox sessions to satisfy the storage quota",
                {"sessions": self._startup_pruned_sessions},
            )

    def _create_session_dir(self, logs_dir: Path) -> Path:
        stamp = datetime.fromtimestamp(self.start_utc_ns / 1_000_000_000, timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
        base = f"{stamp}_{os.getpid()}"
        for suffix in range(1000):
            candidate = logs_dir / (base if suffix == 0 else f"{base}_{suffix}")
            try:
                candidate.mkdir()
                return candidate
            except FileExistsError:
                continue
        raise OSError("could not create a unique blackbox session directory")

    def _elapsed_ns(self) -> int:
        return max(0, time.monotonic_ns() - self.start_monotonic_ns)

    def _put(self, kind: str, value: object) -> None:
        if self._stop_requested or not self._healthy:
            return
        try:
            self._queue.put_nowait((kind, value))
        except queue.Full:
            with self._lock:
                self._dropped_items += 1
                if not self._queue_overflow_reported:
                    message = "blackbox queue overflow; one or more records were dropped"
                    self._queue_overflow_reported = True
                    self._errors.append(message)
                    if not self._error:
                        self._error = message

    def raw(self, data: bytes, connection_id: int) -> None:
        if not data:
            return
        elapsed_ns = self._elapsed_ns()
        utc_ns = time.time_ns()
        view = memoryview(data)
        for offset in range(0, len(view), SBB_MAX_PAYLOAD):
            self._put(
                "raw",
                (elapsed_ns, utc_ns, connection_id, bytes(view[offset : offset + SBB_MAX_PAYLOAD])),
            )

    def event(
        self,
        event_type: str,
        severity: str,
        message: str,
        data: Optional[Mapping[str, object]] = None,
    ) -> None:
        self._put(
            "event",
            {
                "utc_ns": time.time_ns(),
                "utc": utc_text(),
                "elapsed_s": self._elapsed_ns() / 1_000_000_000,
                "severity": severity,
                "type": event_type,
                "message": message,
                "data": dict(data or {}),
            },
        )

    def sample(self, values: Mapping[str, object]) -> None:
        row = dict(values)
        row.setdefault("utc_ns", time.time_ns())
        row.setdefault("utc", utc_text(int(row["utc_ns"])))
        row.setdefault("elapsed_s", self._elapsed_ns() / 1_000_000_000)
        self._put("sample", row)

    def connection(self, connection_id: int, port: str, state: str) -> None:
        item = {
            "connection_id": connection_id,
            "port": port,
            "state": state,
            "utc_ns": time.time_ns(),
            "elapsed_s": self._elapsed_ns() / 1_000_000_000,
        }
        with self._lock:
            self._connections.append(item)
        self.event(f"serial_{state}", "info" if state == "connected" else "warning", f"Serial {state}: {port}", item)

    def status(self) -> Dict[str, object]:
        with self._lock:
            return {
                "enabled": True,
                "healthy": self._healthy,
                "error": self._error,
                "session_dir": str(self.session_dir),
                "session_name": self.session_dir.name,
                "bytes_written": self._bytes_written,
                "session_usage": self._session_usage,
                "total_usage": self._total_usage,
                "quota_bytes": self.config.max_storage_bytes,
                "dropped_items": self._dropped_items,
            }

    def close(self, exit_reason: str = "normal shutdown") -> None:
        if self._closed or self._stop_requested:
            return
        self.event("session_stop", "info", "Blackbox session stopping", {"reason": exit_reason})
        self._stop_requested = True
        if not self._writer_done.is_set():
            try:
                self._queue.put(("stop", exit_reason), timeout=2.0)
            except queue.Full:
                self._set_error("blackbox shutdown failed: writer queue did not drain")
                return
        if not self._writer_done.wait(timeout=15.0):
            self._set_error("blackbox shutdown timed out while draining the writer")
            return
        self._compress_queue.put(None)
        if not self._compressor_done.wait(timeout=30.0):
            self._set_error("blackbox shutdown timed out while finishing compression")
            return
        end_ns = time.time_ns()
        with self._lock:
            self._metadata.update(
                {
                    "state": "completed" if self._healthy and not self._errors else "completed_with_errors",
                    "end_utc_ns": end_ns,
                    "end_utc": utc_text(end_ns),
                    "duration_s": self._elapsed_ns() / 1_000_000_000,
                    "exit_reason": exit_reason,
                    "event_count": self._event_count,
                    "sample_count": self._sample_count,
                    "raw_record_count": self._raw_record_count,
                    "dropped_items": self._dropped_items,
                    "bytes_written": self._bytes_written,
                }
            )
            uncompressed = sum(int(chunk.get("uncompressed_bytes", 0)) for chunk in self._chunks)
            compressed = sum(int(chunk.get("compressed_bytes", 0)) for chunk in self._chunks)
            self._metadata["compression"] = {
                "uncompressed_bytes": uncompressed,
                "compressed_bytes": compressed,
                "ratio": (compressed / uncompressed) if uncompressed else None,
            }
        self._enforce_quota()
        try:
            _atomic_json(self.metadata_path, self._metadata_snapshot())
        except OSError as exc:
            self._set_error(f"metadata close failed: {exc}")
            return
        self._closed = True
        if self._session_lock is not None:
            self._session_lock.release()
            self._session_lock = None

    def _metadata_snapshot(self) -> Dict[str, object]:
        with self._lock:
            snapshot = dict(self._metadata)
            snapshot["connections"] = list(self._connections)
            snapshot["chunks"] = [dict(chunk) for chunk in self._chunks]
            snapshot["pruned_ranges"] = [dict(item) for item in self._pruned_ranges]
            snapshot["errors"] = list(self._errors)
            snapshot["dropped_items"] = self._dropped_items
            snapshot["bytes_written"] = self._bytes_written
            snapshot["session_log_usage"] = self._session_usage
            snapshot["total_log_usage"] = self._total_usage
            return snapshot

    def _set_error(self, message: str) -> None:
        with self._lock:
            self._healthy = False
            self._error = message
            self._errors.append(message)

    def _open_raw(self, elapsed_s: float) -> None:
        self._raw_index += 1
        self._raw_path = self.session_dir / f"serial-{self._raw_index:06d}.sbb"
        self._raw_stream = self._raw_path.open("wb")
        self._raw_stream.write(SBB_HEADER.pack(SBB_MAGIC, self.start_utc_ns))
        self._raw_open_elapsed = elapsed_s

    def _open_csv(self, elapsed_s: float, fields: List[str]) -> None:
        self._csv_index += 1
        self._csv_path = self.session_dir / f"telemetry-{self._csv_index:06d}.csv"
        self._csv_stream = self._csv_path.open("w", encoding="utf-8", newline="")
        self._csv_writer = csv.DictWriter(self._csv_stream, fieldnames=fields, extrasaction="ignore")
        self._csv_writer.writeheader()
        self._csv_open_elapsed = elapsed_s

    def _needs_rotation(self, path: Optional[Path], stream: object, opened_at: float, elapsed_s: float) -> bool:
        if path is None:
            return False
        try:
            too_large = stream.tell() >= self.config.rotate_bytes
        except (AttributeError, OSError):
            too_large = False
        return too_large or elapsed_s - opened_at >= self.config.rotate_seconds

    def _close_chunk(self, kind: str, end_elapsed_s: float) -> None:
        if kind == "raw":
            stream, path, start = self._raw_stream, self._raw_path, self._raw_open_elapsed
            self._raw_stream = None
            self._raw_path = None
        else:
            stream, path, start = self._csv_stream, self._csv_path, self._csv_open_elapsed
            self._csv_stream = None
            self._csv_writer = None
            self._csv_path = None
        if stream is None or path is None:
            return
        stream.flush()
        os.fsync(stream.fileno())
        stream.close()
        size = path.stat().st_size
        with self._lock:
            chunk_index = len(self._chunks)
            self._chunks.append(
                {
                    "kind": kind,
                    "name": path.name,
                    "start_elapsed_s": start,
                    "end_elapsed_s": end_elapsed_s,
                    "uncompressed_bytes": size,
                    "state": "compressing",
                }
            )
        self._write_event(
            {
                "utc_ns": time.time_ns(),
                "utc": utc_text(),
                "elapsed_s": end_elapsed_s,
                "severity": "info",
                "type": "log_rotation",
                "message": f"Closed {kind} chunk {path.name}",
                "data": {"kind": kind, "name": path.name, "bytes": size},
            }
        )
        self._compress_queue.put((path, chunk_index))

    def _write_raw(self, value: object) -> None:
        elapsed_ns, utc_ns, connection_id, data = value
        elapsed_s = elapsed_ns / 1_000_000_000
        if self._raw_stream is None:
            self._open_raw(elapsed_s)
        if self._needs_rotation(self._raw_path, self._raw_stream, self._raw_open_elapsed, elapsed_s):
            self._close_chunk("raw", elapsed_s)
            self._open_raw(elapsed_s)
        header = SBB_RECORD_HEADER.pack(SBB_RECORD_SERIAL, elapsed_ns, utc_ns, connection_id, len(data))
        crc = zlib.crc32(header + data) & 0xFFFFFFFF
        self._raw_stream.write(header + data + SBB_RECORD_CRC.pack(crc))
        with self._lock:
            self._raw_record_count += 1
            self._bytes_written += len(header) + len(data) + SBB_RECORD_CRC.size

    def _write_event(self, value: object) -> None:
        self._events_stream.write(json.dumps(value, separators=(",", ":"), sort_keys=True) + "\n")
        with self._lock:
            self._event_count += 1

    def _write_sample(self, value: object) -> None:
        row = dict(value)
        elapsed_s = float(row["elapsed_s"])
        if self._csv_fields is None:
            self._csv_fields = list(row.keys())
        if self._csv_stream is None:
            self._open_csv(elapsed_s, self._csv_fields)
        if self._needs_rotation(self._csv_path, self._csv_stream, self._csv_open_elapsed, elapsed_s):
            self._close_chunk("csv", elapsed_s)
            self._open_csv(elapsed_s, self._csv_fields)
        self._csv_writer.writerow(row)
        with self._lock:
            self._sample_count += 1

    def _flush(self, sync: bool) -> None:
        streams = [stream for stream in (self._raw_stream, self._csv_stream, self._events_stream) if stream is not None]
        for stream in streams:
            stream.flush()
            if sync:
                os.fsync(stream.fileno())
        session_usage = _flat_directory_size(self.session_dir)
        with self._lock:
            self._total_usage = max(0, self._total_usage + session_usage - self._session_usage)
            self._session_usage = session_usage

    def _writer_loop(self) -> None:
        last_flush = time.monotonic()
        last_sync = last_flush
        last_metadata = last_flush
        exit_reason = "normal shutdown"
        try:
            while True:
                try:
                    kind, value = self._queue.get(timeout=0.25)
                except queue.Empty:
                    kind, value = "", None
                if kind == "stop":
                    exit_reason = str(value)
                    break
                if kind == "raw":
                    self._write_raw(value)
                elif kind == "event":
                    self._write_event(value)
                elif kind == "sample":
                    self._write_sample(value)

                now = time.monotonic()
                if now - last_flush >= self.config.flush_seconds:
                    sync = now - last_sync >= self.config.fsync_seconds
                    self._flush(sync)
                    last_flush = now
                    if sync:
                        last_sync = now
                    if now - last_metadata >= 10.0:
                        self._enforce_quota()
                        _atomic_json(self.metadata_path, self._metadata_snapshot())
                        last_metadata = now
        except Exception as exc:
            self._set_error(f"blackbox writer failed: {exc}")
        finally:
            elapsed_s = self._elapsed_ns() / 1_000_000_000
            try:
                self._close_chunk("raw", elapsed_s)
                self._close_chunk("csv", elapsed_s)
                self._flush(True)
            except Exception as exc:
                self._set_error(f"blackbox final flush failed: {exc}")
            try:
                self._events_stream.close()
            except Exception:
                pass
            with self._lock:
                self._metadata["exit_reason"] = exit_reason
            self._writer_done.set()

    def _compress_loop(self) -> None:
        try:
            while True:
                task = self._compress_queue.get()
                if task is None:
                    return
                self._compress_chunk(*task)
        finally:
            self._compressor_done.set()

    def _compress_chunk(self, source: Path, chunk_index: int) -> None:
        destination = source.with_suffix(source.suffix + ".gz")
        temporary = destination.with_suffix(destination.suffix + ".tmp")
        try:
            source_size = source.stat().st_size
            with source.open("rb") as input_stream, gzip.open(
                temporary, "wb", compresslevel=self.config.gzip_level
            ) as output_stream:
                shutil.copyfileobj(input_stream, output_stream, length=1024 * 1024)
            decompressed_size = 0
            with gzip.open(temporary, "rb") as verify_stream:
                while True:
                    block = verify_stream.read(1024 * 1024)
                    if not block:
                        break
                    decompressed_size += len(block)
            if decompressed_size != source_size:
                raise OSError("compressed chunk verification failed")
            os.replace(temporary, destination)
            source.unlink()
            with self._lock:
                chunk = self._chunks[chunk_index]
                chunk.update(
                    {
                        "state": "compressed",
                        "compressed_name": destination.name,
                        "compressed_bytes": destination.stat().st_size,
                    }
                )
        except Exception as exc:
            try:
                temporary.unlink(missing_ok=True)
            except OSError:
                pass
            with self._lock:
                self._chunks[chunk_index]["state"] = "compression_failed"
                self._errors.append(f"compression failed for {source.name}: {exc}")
                self._error = self._errors[-1]
                if isinstance(exc, OSError) and exc.errno in (errno.ENOSPC, errno.EACCES, errno.EROFS):
                    self._healthy = False

    def _prune_old_sessions_locked(self, exclude: Optional[Path]) -> List[str]:
        removed: List[str] = []
        quota = self.config.max_storage_bytes
        if quota <= 0 or not self.config.logs_dir.exists():
            return removed
        target = int(quota * self.config.target_storage_ratio)
        usage = directory_size(self.config.logs_dir)
        if usage <= quota:
            return removed
        for _start, path, metadata in _session_directories(self.config.logs_dir):
            if exclude is not None and path == exclude:
                continue
            if metadata.get("state") not in ("completed", "completed_with_errors", "interrupted"):
                continue
            session_lock = _AdvisoryFileLock(path / ".active.lock")
            if not session_lock.acquire(blocking=False):
                continue
            session_lock.release()
            try:
                shutil.rmtree(path)
            except OSError:
                continue
            removed.append(path.name)
            usage = directory_size(self.config.logs_dir)
            if usage <= target:
                break
        return removed

    def _prune_old_sessions(self, exclude: Optional[Path]) -> List[str]:
        with _exclusive_lock(self.config.logs_dir / ".quota.lock"):
            return self._prune_old_sessions_locked(exclude)

    def _enforce_quota(self) -> None:
        with _exclusive_lock(self.config.logs_dir / ".quota.lock"):
            self._enforce_quota_locked()

    def _enforce_quota_locked(self) -> None:
        quota = self.config.max_storage_bytes
        if quota <= 0:
            usage = directory_size(self.config.logs_dir)
            session_usage = _flat_directory_size(self.session_dir)
            with self._lock:
                self._session_usage = session_usage
                self._total_usage = usage
            return
        removed_sessions = self._prune_old_sessions_locked(exclude=self.session_dir)
        if removed_sessions and not self._stop_requested:
            self.event(
                "storage_pruned",
                "info",
                "Removed old blackbox sessions to satisfy the storage quota",
                {"sessions": removed_sessions},
            )
        target = int(quota * self.config.target_storage_ratio)
        usage = directory_size(self.config.logs_dir)
        if usage > quota:
            with self._lock:
                available = [
                    (index, chunk)
                    for index, chunk in enumerate(self._chunks)
                    if chunk.get("state") in ("compressed", "compression_failed") and not chunk.get("pruned")
                ]
                protected = set()
                for kind in ("raw", "csv"):
                    kind_items = [item for item in available if item[1].get("kind") == kind]
                    protected.update(index for index, _chunk in kind_items[-2:])
                candidates = [item for item in available if item[0] not in protected]
            for index, chunk in candidates:
                filename = str(chunk.get("compressed_name") or chunk.get("name"))
                path = self.session_dir / filename
                try:
                    size = path.stat().st_size
                    path.unlink()
                except OSError:
                    continue
                with self._lock:
                    chunk["pruned"] = True
                    chunk["state"] = "pruned"
                    pruned = {
                        "kind": chunk.get("kind"),
                        "name": filename,
                        "start_elapsed_s": chunk.get("start_elapsed_s"),
                        "end_elapsed_s": chunk.get("end_elapsed_s"),
                        "bytes": size,
                    }
                    self._pruned_ranges.append(pruned)
                if not self._stop_requested:
                    self.event(
                        "storage_chunk_pruned",
                        "warning",
                        f"Pruned completed blackbox chunk {filename}",
                        pruned,
                    )
                usage = directory_size(self.config.logs_dir)
                if usage <= target:
                    break
        session_usage = _flat_directory_size(self.session_dir)
        with self._lock:
            self._session_usage = session_usage
            self._total_usage = usage


class DisabledBlackbox:
    """No-op recorder used when blackbox recording is disabled or unavailable."""

    def __init__(self, error: str = "") -> None:
        self.error = error

    def raw(self, data: bytes, connection_id: int) -> None:
        pass

    def event(self, event_type: str, severity: str, message: str, data: Optional[Mapping[str, object]] = None) -> None:
        pass

    def sample(self, values: Mapping[str, object]) -> None:
        pass

    def connection(self, connection_id: int, port: str, state: str) -> None:
        pass

    def status(self) -> Dict[str, object]:
        return {
            "enabled": False,
            "healthy": not bool(self.error),
            "error": self.error,
            "session_dir": "",
            "session_name": "",
            "bytes_written": 0,
            "session_usage": 0,
            "total_usage": 0,
            "quota_bytes": 0,
            "dropped_items": 0,
        }

    def close(self, exit_reason: str = "normal shutdown") -> None:
        pass
