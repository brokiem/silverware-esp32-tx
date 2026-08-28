import gzip
import dataclasses
import errno
import json
import sys
import tempfile
import time
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock


TOOL_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOL_DIR))

from blackbox import (  # noqa: E402
    BlackboxConfig,
    BlackboxFormatError,
    BlackboxRecorder,
    inspect_session,
    iter_sbb_records,
    parse_storage_size,
    recover_interrupted_sessions,
)
from ground_station import (  # noqa: E402
    FlightEventTracker,
    TelemetryModel,
    blackbox_sample_period,
    parse_args,
)


class RecordingFixture(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.logs_dir = Path(self.temporary.name) / "logs"

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def recorder(self, **overrides: object) -> BlackboxRecorder:
        values = {
            "logs_dir": self.logs_dir,
            "max_storage_bytes": 0,
            "rotate_bytes": 1024 * 1024,
            "rotate_seconds": 3600.0,
            "flush_seconds": 0.01,
            "fsync_seconds": 0.01,
        }
        values.update(overrides)
        return BlackboxRecorder(BlackboxConfig(**values), "/dev/ttyUSB0", 115200, ["ground_station.py"])


class BlackboxFormatTests(RecordingFixture):
    def test_session_writes_raw_csv_events_and_metadata(self) -> None:
        recorder = self.recorder()
        recorder.connection(1, "/dev/ttyUSB0", "connected")
        recorder.raw(b"boot\x00frame", 1)
        recorder.sample({"value": 42, "armed": False})
        recorder.event("test_event", "info", "test message", {"answer": 42})
        recorder.close("unit test")

        files = {path.name for path in recorder.session_dir.iterdir()}
        self.assertIn("session.json", files)
        self.assertIn("events.jsonl", files)
        self.assertIn("serial-000001.sbb.gz", files)
        self.assertIn("telemetry-000001.csv.gz", files)
        records = list(iter_sbb_records(recorder.session_dir / "serial-000001.sbb.gz"))
        self.assertEqual(records[0].data, b"boot\x00frame")
        self.assertEqual(records[0].connection_id, 1)

        metadata = json.loads((recorder.session_dir / "session.json").read_text(encoding="utf-8"))
        self.assertEqual(metadata["state"], "completed")
        self.assertEqual(metadata["exit_reason"], "unit test")
        self.assertGreaterEqual(metadata["event_count"], 3)
        self.assertEqual(metadata["sample_count"], 1)

    def test_consecutive_runs_create_unique_sessions(self) -> None:
        first = self.recorder()
        first.close()
        second = self.recorder()
        second.close()

        self.assertNotEqual(first.session_dir, second.session_dir)
        self.assertTrue(first.session_dir.exists())
        self.assertTrue(second.session_dir.exists())

    def test_corrupted_raw_record_is_rejected(self) -> None:
        recorder = self.recorder()
        recorder.raw(b"payload", 7)
        recorder.close()
        compressed = recorder.session_dir / "serial-000001.sbb.gz"
        with gzip.open(compressed, "rb") as stream:
            contents = bytearray(stream.read())
        contents[-1] ^= 0xFF
        damaged = recorder.session_dir / "damaged.sbb"
        damaged.write_bytes(contents)

        with self.assertRaises(BlackboxFormatError):
            list(iter_sbb_records(damaged))

    def test_large_serial_reads_are_split_into_valid_records(self) -> None:
        recorder = self.recorder()
        payload = bytes(range(256)) * 300
        recorder.raw(payload, 3)
        recorder.close()

        records = list(iter_sbb_records(recorder.session_dir / "serial-000001.sbb.gz"))
        self.assertEqual(b"".join(record.data for record in records), payload)
        self.assertEqual(len(records), 2)

    def test_inspector_verifies_and_summarizes_session(self) -> None:
        recorder = self.recorder()
        recorder.raw(b"payload", 4)
        recorder.sample(
            {
                "fc_battery_compensated_v": 3.71,
                "fc_maximum_rx_gap_ms": 8.5,
                "fc_armed": True,
                "fc_flight_mode": "Acro",
            }
        )
        recorder.event("low_voltage", "warning", "Low voltage", {})
        recorder.close()

        report = inspect_session(self.logs_dir)

        self.assertEqual(report["session_dir"], str(recorder.session_dir))
        self.assertEqual(report["raw_records"], 1)
        self.assertEqual(report["minimum_battery_v"], 3.71)
        self.assertEqual(report["maximum_rx_gap_ms"], 8.5)
        self.assertEqual(report["modes"], ["Acro"])
        self.assertFalse(report["integrity_errors"])

    def test_rotation_creates_verified_compressed_chunks(self) -> None:
        recorder = self.recorder(rotate_bytes=64)
        for index in range(8):
            recorder.raw(bytes([index]) * 32, 1)
            recorder.sample({"index": index, "text": "x" * 80})
        recorder.close()

        raw_chunks = sorted(recorder.session_dir.glob("serial-*.sbb.gz"))
        csv_chunks = sorted(recorder.session_dir.glob("telemetry-*.csv.gz"))
        self.assertGreaterEqual(len(raw_chunks), 2)
        self.assertGreaterEqual(len(csv_chunks), 2)
        self.assertEqual(sum(len(list(iter_sbb_records(path))) for path in raw_chunks), 8)
        for path in csv_chunks:
            with gzip.open(path, "rt", encoding="utf-8") as stream:
                self.assertTrue(stream.readline().startswith("index,text"))

    def test_compression_failure_retains_original_chunk(self) -> None:
        recorder = self.recorder()
        recorder.raw(b"payload", 1)
        with mock.patch("blackbox.gzip.open", side_effect=OSError("compressor unavailable")):
            recorder.close()

        self.assertTrue((recorder.session_dir / "serial-000001.sbb").exists())
        metadata = json.loads((recorder.session_dir / "session.json").read_text(encoding="utf-8"))
        self.assertEqual(metadata["state"], "completed_with_errors")
        self.assertEqual(metadata["chunks"][0]["state"], "compression_failed")
        self.assertTrue(any("compression failed" in error for error in metadata["errors"]))

    def test_disk_full_during_compression_marks_recorder_unhealthy(self) -> None:
        recorder = self.recorder()
        recorder.raw(b"payload", 1)
        failure = OSError(errno.ENOSPC, "disk full")
        with mock.patch("blackbox.gzip.open", side_effect=failure):
            recorder.close()

        self.assertFalse(recorder.status()["healthy"])
        self.assertIn("compression failed", str(recorder.status()["error"]))

    def test_writer_failure_is_reported_without_raising_in_producer(self) -> None:
        recorder = self.recorder()
        original_open = Path.open

        def failing_open(path: Path, *args: object, **kwargs: object):
            if path.name.startswith("serial-"):
                raise OSError("disk unavailable")
            return original_open(path, *args, **kwargs)

        with mock.patch("blackbox.Path.open", autospec=True, side_effect=failing_open):
            recorder.raw(b"payload", 1)
            time.sleep(0.05)

        self.assertFalse(recorder.status()["healthy"])
        self.assertIn("writer failed", str(recorder.status()["error"]))
        recorder.close()

    def test_regular_flush_does_not_rescan_the_entire_logs_tree(self) -> None:
        recorder = self.recorder(flush_seconds=0.01, fsync_seconds=3600.0)
        with mock.patch("blackbox.directory_size", side_effect=AssertionError("unexpected directory scan")):
            recorder.raw(b"payload", 1)
            time.sleep(0.05)
        recorder.close()


class StoragePolicyTests(RecordingFixture):
    def _old_session(self, name: str, start_ns: int, size: int, state: str = "completed") -> Path:
        path = self.logs_dir / name
        path.mkdir(parents=True, exist_ok=True)
        (path / "payload.bin").write_bytes(b"x" * size)
        (path / "session.json").write_text(
            json.dumps({"schema_version": 1, "state": state, "start_utc_ns": start_ns}),
            encoding="utf-8",
        )
        return path

    def test_oldest_completed_session_is_pruned_to_quota_target(self) -> None:
        oldest = self._old_session("oldest", 1, 10_000)
        newest = self._old_session("newest", 2, 10_000)

        recorder = self.recorder(max_storage_bytes=18_000, target_storage_ratio=0.9)
        recorder.close()

        self.assertFalse(oldest.exists())
        self.assertTrue(newest.exists())

    def test_active_sessions_are_recovered_as_interrupted(self) -> None:
        abandoned = self._old_session("abandoned", 1, 10, state="active")

        recover_interrupted_sessions(self.logs_dir)

        metadata = json.loads((abandoned / "session.json").read_text(encoding="utf-8"))
        self.assertEqual(metadata["state"], "interrupted")
        self.assertIn("end_utc_ns", metadata)

    def test_live_session_is_not_recovered_as_interrupted(self) -> None:
        recorder = self.recorder()

        recover_interrupted_sessions(self.logs_dir)

        metadata = json.loads((recorder.session_dir / "session.json").read_text(encoding="utf-8"))
        self.assertEqual(metadata["state"], "active")
        recorder.close()

    def test_quota_pruning_skips_a_locked_session(self) -> None:
        first = self.recorder()
        metadata_path = first.session_dir / "session.json"
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        metadata["state"] = "completed"
        metadata_path.write_text(json.dumps(metadata), encoding="utf-8")
        (first.session_dir / "large.bin").write_bytes(b"x" * 20_000)

        second = self.recorder(max_storage_bytes=1_000)

        self.assertTrue(first.session_dir.exists())
        second.close()
        first.close()

    def test_zero_quota_disables_pruning(self) -> None:
        old = self._old_session("old", 1, 100)

        recorder = self.recorder(max_storage_bytes=0)
        recorder.close()

        self.assertTrue(old.exists())

    def test_long_active_session_prunes_old_chunks_but_keeps_latest_two(self) -> None:
        recorder = self.recorder(rotate_bytes=64)
        for index in range(12):
            recorder.raw(bytes(range(64)), 1)
        recorder.close()

        initial_chunks = [chunk for chunk in recorder._chunks if chunk["kind"] == "raw"]
        self.assertGreaterEqual(len(initial_chunks), 4)
        usage = sum(path.stat().st_size for path in recorder.session_dir.iterdir() if path.is_file())
        recorder.config = dataclasses.replace(
            recorder.config,
            max_storage_bytes=max(1, usage - 1),
            target_storage_ratio=0.5,
        )

        recorder._enforce_quota()

        remaining = [chunk for chunk in recorder._chunks if chunk["kind"] == "raw" and not chunk.get("pruned")]
        self.assertGreaterEqual(len(remaining), 2)
        self.assertTrue(any(chunk.get("pruned") for chunk in initial_chunks))
        self.assertTrue(recorder._pruned_ranges)


class BlackboxBehaviorTests(unittest.TestCase):
    def test_storage_size_parser(self) -> None:
        self.assertEqual(parse_storage_size("0"), 0)
        self.assertEqual(parse_storage_size("512M"), 512 * 1024**2)
        self.assertEqual(parse_storage_size("2G"), 2 * 1024**3)
        with self.assertRaises(ValueError):
            parse_storage_size("many")
        with self.assertRaises(ValueError):
            parse_storage_size("inf")

    def test_cli_parses_blackbox_options(self) -> None:
        args = parse_args(["--logs-dir", "/tmp/logs", "--max-log-storage", "512M", "--no-blackbox"])
        self.assertEqual(args.logs_dir, Path("/tmp/logs"))
        self.assertEqual(args.max_log_storage, 512 * 1024**2)
        self.assertTrue(args.no_blackbox)

    def test_adaptive_sample_period(self) -> None:
        model = TelemetryModel()
        self.assertIsNone(blackbox_sample_period(model))
        model.last_any_at = 1.0
        self.assertEqual(blackbox_sample_period(model), 0.5)
        model.fc.armed = True
        self.assertEqual(blackbox_sample_period(model), 0.1)
        model.fc.armed = False
        model.local = SimpleNamespace(status_flags=1 << 3)
        self.assertEqual(blackbox_sample_period(model), 0.1)

    def test_event_tracker_deduplicates_unchanged_state(self) -> None:
        class Recorder:
            def __init__(self) -> None:
                self.events = []

            def event(self, *args: object) -> None:
                self.events.append(args)

        recorder = Recorder()
        tracker = FlightEventTracker()
        model = TelemetryModel()
        model.fc.protocol = "Extended V1"
        model.fc.armed = True
        update = SimpleNamespace(esp_restarted=False, sequence_gap=0, message_type=1)

        tracker.accept(recorder, model, update)
        first_count = len(recorder.events)
        tracker.accept(recorder, model, update)

        self.assertGreater(first_count, 0)
        self.assertEqual(len(recorder.events), first_count)


if __name__ == "__main__":
    unittest.main()
