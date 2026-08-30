# Build and test

[Project overview](../README.md)

## Toolchain

The project uses ESP-IDF 5.5.5 through the pinned PIO Arduino platform. It declares Arduino-ESP32 3.3.11 through the IDF Component Manager.

The repository contains `dependencies.lock`. Git ignores the generated `managed_components/` directory.

The build must work from a clean checkout. Do not add a local `components/arduino` symbolic link.

## Run native tests

Run this command from the project root:

```sh
pio test -e native
```

The tests compile production protocol, mapping, telemetry, failsafe, radio validation, and storage code.

## Build the ESP32 firmware

Run this command from the project root:

```sh
pio run -e esp32dev
```

The optional binary USB telemetry build uses a separate environment:

```sh
pio run -e esp32dev-pc-telemetry
```

Its wire format is documented in [PC telemetry bridge](pc-telemetry-bridge.md).

The build downloads managed dependencies when necessary.

## Flash the ESP32

Connect the ESP32 with a USB cable. Then run this command:

```sh
pio run -e esp32dev -t upload
```

## Required checks

Run these checks before you commit:

```sh
pio test -e native
pio run -e esp32dev
pio run -e esp32dev-pc-telemetry
```

Also run this check for whitespace errors:

```sh
git diff --check
```

Use a clean checkout to confirm that the dependency setup is complete.
