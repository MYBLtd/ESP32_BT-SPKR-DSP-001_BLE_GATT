# Building

## Overview

This repository is intended as a practical starting point for experimentation with the ChaoticVolt BLE-I2S bridge component.

It is primarily useful for people who want to:

- build and flash the bridge firmware
- inspect the current ESP32-side implementation
- experiment with BLE GATT control, Bluetooth audio ingress, and I2S handoff
- use the codebase as a starting point for related projects

For architectural context, see [`README.md`](README.md) and the documents in [`docs/`](docs/).

## Build baseline

This repository is currently maintained against **ESP-IDF v6**.

The project should be treated as **ESP-IDF v6-first** unless noted otherwise. Earlier ESP-IDF versions may require manual fixes, configuration changes, or API updates, and should not be assumed to be drop-in compatible.

## Target environment

Current assumptions:

- **Framework:** ESP-IDF v6
- **Primary platform:** ESP32-class target used for the BLE-I2S bridge role
- **Build system:** CMake via ESP-IDF tooling
- **Configuration files:** `sdkconfig`, `sdkconfig.defaults`

If you are experimenting from a fresh environment, start by making sure your ESP-IDF installation is working normally before troubleshooting project-specific issues.

## Prerequisites

Before building, make sure you have:

- ESP-IDF v6 installed
- the ESP-IDF environment exported in your shell
- a supported ESP32 development target connected over USB
- serial access permissions configured correctly on your machine
- the expected toolchain components installed through ESP-IDF

If your ESP-IDF environment is not already active, activate it first using your normal setup flow.

## Typical workflow

A common build and flash workflow looks like this:

```bash
idf.py build
idf.py -p /dev/ttyUSB0 flash
idf.py -p /dev/ttyUSB0 monitor
```

On macOS, your serial device may look more like:

```text
/dev/cu.usbserial-xxxx
/dev/cu.SLAB_USBtoUART
/dev/cu.usbmodemxxxx
```

Adjust the serial port to match your actual device.

## Recommended first build

If you are starting from a clean checkout, a safe first pass is:

```bash
idf.py fullclean
idf.py build
```

This helps avoid confusion from stale generated artifacts.

## Configuration notes

This repository includes ESP-IDF configuration files that reflect the current working baseline.

Important files include:

- `sdkconfig`
- `sdkconfig.defaults`
- `partitions_ota.csv`

Treat these as part of the current build baseline rather than as incidental leftovers.

## Flashing

Once the build succeeds, flash the firmware to the connected device:

```bash
idf.py -p /dev/ttyUSB0 flash
```

Then open the serial monitor:

```bash
idf.py -p /dev/ttyUSB0 monitor
```

If you use a different baud rate, port, or target setup, adjust accordingly.

## Clean rebuilds

If the project starts behaving strangely after environment or configuration changes, try a clean rebuild:

```bash
idf.py fullclean
idf.py build
```

If needed, recheck that you are still using **ESP-IDF v6** and not an older shell environment.

## Known build expectations

When working with this repository, assume:

- the code is aligned with the current bridge role of the project
- older README or legacy notes may reflect earlier architectural phases
- ESP-IDF API differences across major versions can matter
- audio, BLE, and UART integration code may depend on configuration details that are easy to break with a mismatched environment

## Good starting points for experimentation

If you are using this repository as a starting point for your own experiments, useful areas to inspect first are:

- BLE GATT handling
- Bluetooth audio ingress
- I2S handoff behavior
- UART control relay logic
- settings persistence and platform glue

For the higher-level platform story, see:

- [`docs/overview.md`](docs/overview.md)
- [`docs/architecture.md`](docs/architecture.md)
- [`docs/audio-path.md`](docs/audio-path.md)
- [`docs/control-path.md`](docs/control-path.md)

## Troubleshooting mindset

If something fails early, check these first:

- are you really building against **ESP-IDF v6**?
- is the correct serial port selected?
- is the ESP-IDF environment exported in the current shell?
- are you working from a clean build directory?
- does your target hardware match the assumptions of the current configuration?

Many confusing failures in ESP-IDF projects turn out to be environment mismatch rather than code issues.

## Notes

This file is meant to keep the build story visible and practical for people who want to use the repository as a starting point.

It is intentionally lightweight. If the build process becomes more specialized over time, this file can grow to include target-specific steps, dependency notes, and common gotchas.

