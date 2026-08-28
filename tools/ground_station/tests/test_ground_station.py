import io
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace


TOOL_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOL_DIR))

from ground_station import (  # noqa: E402
    BridgeRecord,
    Dashboard,
    LocalState,
    MESSAGE_RAW_FC,
    SERIAL_SILENCE_TIMEOUT_S,
    ScreenSize,
    TelemetryModel,
    TerminalRenderer,
    open_serial_port,
    serial_candidates,
    serial_connection_stale,
    visible_len,
)


class TelemetryModelTests(unittest.TestCase):
    def test_esp_reboot_discards_pre_reboot_aggregate_state(self) -> None:
        model = TelemetryModel()
        model.local = SimpleNamespace(status_flags=0xFFFF)
        model.last_type1_at = 1.0
        model.last_type2_at = 1.0
        model._last_esp_timestamp = 100
        packet = bytes([0x85]) + bytes(13) + bytes([0x85])

        update = model.accept(BridgeRecord(MESSAGE_RAW_FC, 0, 1, packet), now=2.0)

        self.assertTrue(update.esp_restarted)
        self.assertIsNone(model.local)
        self.assertIsNone(model.last_type2_at)
        self.assertEqual(model.last_type1_at, 2.0)


class DashboardLayoutTests(unittest.TestCase):
    def test_nominal_status_is_hidden(self) -> None:
        dashboard = Dashboard(use_color=False)
        model = TelemetryModel()
        model.fc.protocol = "Extended V1"
        model.last_any_at = 1.0

        alert = dashboard._alert_line(
            model,
            SimpleNamespace(status_flags=0),
            bridge_live=True,
            local_live=True,
            fc_live=True,
        )

        self.assertEqual(alert, "")

    def test_blackbox_failure_is_an_active_alert(self) -> None:
        dashboard = Dashboard(use_color=False)
        model = TelemetryModel()
        model.fc.protocol = "Extended V1"
        model.last_any_at = 1.0

        alert = dashboard._alert_line(
            model,
            SimpleNamespace(status_flags=0),
            bridge_live=True,
            local_live=True,
            fc_live=True,
            blackbox_status={"enabled": True, "error": "disk full"},
        )

        self.assertIn("BLACKBOX WRITE FAILED", alert)

    def test_wide_header_separates_status_groups(self) -> None:
        dashboard = Dashboard(use_color=False)
        header = dashboard._header(
            TelemetryModel(),
            "/dev/ttyUSB0",
            now=1.0,
            width=159,
            bridge_live=False,
            local_live=False,
            fc_live=False,
        )

        self.assertTrue(header[0].startswith("+"))
        self.assertTrue(header[0].endswith("+"))
        self.assertTrue(header[1].startswith("| "))
        self.assertTrue(header[1].endswith(" |"))
        self.assertTrue(header[2].startswith("+"))
        self.assertTrue(header[2].endswith("+"))
        self.assertLess(header[3].index("LINK / DATA"), header[3].index("CONTROL STATE"))
        self.assertLess(header[3].index("CONTROL STATE"), header[3].index("FLIGHT / POWER"))
        self.assertLess(header[4].index("[FC:WAIT]"), header[4].index("[CONTROL:--]"))
        self.assertLess(header[4].index("[CONTROL:--]"), header[4].index("[ARM:--]"))

    def test_panel_values_share_one_column(self) -> None:
        dashboard = Dashboard(use_color=False)
        rows = (
            dashboard.field("Low voltage", "clear"),
            dashboard.field("Page A/control age", "0 ms"),
            dashboard.field("System state", "ACTIVE"),
            dashboard.field("FC accepted/reject", "10 / 2"),
        )

        for row, value in zip(rows, ("clear", "0 ms", "ACTIVE", "10 / 2")):
            with self.subTest(row=row):
                self.assertEqual(row.index(value), Dashboard.FIELD_VALUE_COLUMN)

    def test_every_row_fits_the_requested_width(self) -> None:
        dashboard = Dashboard(use_color=True)
        waiting_model = TelemetryModel()
        live_model = TelemetryModel()
        live_model.fc.protocol = "Extended V1"
        live_model.fc.page_mask = 0x0F
        live_model.fc.flight_mode = "Horizon"
        live_model.fc.page_received_at = {page: 1.0 for page in range(4)}
        live_model.local = LocalState(
            system_state=4,
            status_flags=0x07FF,
            buttons=0x3FFF,
            aux_modes=0x1F,
            consecutive_tx_failures=255,
            next_hopping_channel=3,
            roll_raw=-32768,
            pitch_raw=32767,
            yaw_raw=-32768,
            throttle_raw=65535,
            gamepad_age_ms=65534,
            fc_telemetry_age_ms=65534,
            tx_packets=0xFFFFFFFF,
            tx_failures=0xFFFFFFFF,
            telemetry_accepted=0xFFFFFFFF,
            telemetry_rejected=0xFFFFFFFF,
            deadline_misses=0xFFFFFFFF,
            export_queue_drops=0xFFFFFFFF,
        )
        live_model.last_any_at = 1.0
        live_model.last_type1_at = 1.0
        live_model.last_type2_at = 1.0

        for model in (waiting_model, live_model):
            for width in (8, 24, 48, 71, 72, 95, 96, 119, 160):
                with self.subTest(protocol=model.fc.protocol, width=width):
                    frame = dashboard.render(model, "auto-detecting", now=1.1, width=width)
                    self.assertLessEqual(
                        max(visible_len(line) for line in frame.splitlines()),
                        width,
                    )


class TerminalRendererTests(unittest.TestCase):
    def setUp(self) -> None:
        self.output = io.StringIO()
        self.renderer = TerminalRenderer(self.output, interactive=True)
        self.size = ScreenSize(width=40, height=10)

    def take_output(self) -> str:
        value = self.output.getvalue()
        self.output.seek(0)
        self.output.truncate(0)
        return value

    def test_shorter_row_is_erased_before_it_is_rewritten(self) -> None:
        self.renderer.draw("MODE: HORIZON  BAT: 4.200V", self.size)
        self.take_output()

        self.renderer.draw("MODE: RATE  BAT: 4.1V", self.size)

        update = self.take_output()
        self.assertIn("\033[1;1H\033[0m\033[2KMODE: RATE  BAT: 4.1V", update)
        self.assertNotIn("4.200V", update)

    def test_unchanged_rows_are_not_rewritten(self) -> None:
        self.renderer.draw("one\ntwo\nthree", self.size)
        self.take_output()

        self.renderer.draw("one\nchanged\nthree", self.size)

        self.assertEqual(
            self.take_output(),
            "\033[2;1H\033[0m\033[2Kchanged\033[H",
        )

    def test_rows_removed_from_a_frame_are_cleared(self) -> None:
        self.renderer.draw("one\ntwo\nthree", self.size)
        self.take_output()

        self.renderer.draw("one", self.size)

        update = self.take_output()
        self.assertIn("\033[2;1H\033[0m\033[2K", update)
        self.assertIn("\033[3;1H\033[0m\033[2K", update)

    def test_resize_forces_a_clean_redraw(self) -> None:
        self.renderer.draw("one", self.size)
        self.take_output()

        self.renderer.draw("one", ScreenSize(width=30, height=8))

        self.assertTrue(self.take_output().startswith("\033[2J\033[H"))

    def test_tall_frames_are_clipped_without_scrolling(self) -> None:
        self.renderer.draw("one\ntwo\nthree", ScreenSize(width=20, height=2))

        update = self.take_output()
        self.assertNotIn("\033[3;1H", update)
        self.assertIn("[dashboard clipped:", update)


class SerialReconnectTests(unittest.TestCase):
    def test_requested_port_falls_back_to_reenumerated_device(self) -> None:
        detected = (
            SimpleNamespace(device="/dev/ttyUSB1"),
            SimpleNamespace(device="/dev/ttyACM0"),
        )

        self.assertEqual(
            serial_candidates("/dev/ttyUSB0", detected),
            ["/dev/ttyUSB0", "/dev/ttyUSB1", "/dev/ttyACM0"],
        )

    def test_candidate_devices_are_not_duplicated(self) -> None:
        detected = (SimpleNamespace(device="/dev/ttyUSB0"),)

        self.assertEqual(serial_candidates("/dev/ttyUSB0", detected), ["/dev/ttyUSB0"])

    def test_builtin_linux_serial_ports_are_not_auto_selected(self) -> None:
        detected = (
            SimpleNamespace(device="/dev/ttyS0", description="n/a", hwid="n/a"),
            SimpleNamespace(device="/dev/ttyUSB0", description="CP2102", hwid="USB VID:PID=10C4:EA60"),
        )

        self.assertEqual(serial_candidates(None, detected), ["/dev/ttyUSB0"])

    def test_serial_opens_with_reset_lines_deasserted(self) -> None:
        class FakeConnection:
            def __init__(self) -> None:
                self.dtr = True
                self.rts = True
                self.is_open = False
                self.opened_with = None

            def open(self) -> None:
                self.opened_with = (self.dtr, self.rts)
                self.is_open = True

            def close(self) -> None:
                self.is_open = False

        connection = FakeConnection()
        serial_module = SimpleNamespace(Serial=lambda: connection)

        opened = open_serial_port(serial_module, "/dev/ttyUSB0", 115200)

        self.assertIs(opened, connection)
        self.assertEqual(connection.opened_with, (False, False))
        self.assertEqual(connection.port, "/dev/ttyUSB0")
        self.assertEqual(connection.baudrate, 115200)

    def test_silent_or_closed_connection_is_stale(self) -> None:
        connection = SimpleNamespace(is_open=True)

        self.assertFalse(serial_connection_stale(connection, 10.0, 10.0 + SERIAL_SILENCE_TIMEOUT_S - 0.01))
        self.assertTrue(serial_connection_stale(connection, 10.0, 10.0 + SERIAL_SILENCE_TIMEOUT_S))

        connection.is_open = False
        self.assertTrue(serial_connection_stale(connection, 10.0, 10.1))


if __name__ == "__main__":
    unittest.main()
