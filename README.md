# ESP32 A2DP Bluetooth Sink with BLE GATT Relay

Bluetooth A2DP audio receiver and BLE GATT relay for the 42 Decibels audio platform. The ESP32 receives audio over Bluetooth (A2DP) and forwards it via I2S to the STM32 DSP engine. BLE GATT commands from the 42 Decibels app are forwarded to the STM32 over UART — all DSP processing runs on the STM32.

> **Note:** Prior to v2.4.0 this firmware included its own DSP processing chain (biquad EQ, loudness, normalizer, limiter). That code was removed in v2.4.0 when DSP was moved to the dedicated STM32 DSP engine. See [Legacy: ESP32-side DSP (≤ v2.3.1)](#legacy-esp32-side-dsp--v231) below.

**Version:** 2.4.3 | **Date:** 2026-02-18

## Features

- **Bluetooth A2DP Sink** — Receive high-quality audio from phones, tablets, and computers
- **I2S Audio Output** — Streams received audio to I2S DAC or STM32 DSP engine
- **BLE GATT Control** — Receives DSP commands wirelessly from the 42 Decibels iOS/Watch app
- **UART DSP Relay** — Forwards BLE commands to external STM32 DSP engine via UART (GPIO4/GPIO5 @ 115200)
- **GalacticStatus** — Extended status reporting with periodic BLE notifications (2×/sec)
- **OTA Firmware Updates** — Over-the-air updates via BLE provisioning + WiFi download
- **Persistent Settings** — Settings survive power cycles via NVS flash storage
- **iOS Compatible** — Secure Simple Pairing (SSP) for seamless iOS pairing
- **BT RSSI Monitoring** — Reads Bluetooth signal strength every 10s when A2DP is active

## Hardware Requirements

- ESP32-WROOM-32 or compatible module
- I2S DAC (e.g., MAX98357A, PCM5102A)
- Speaker/amplifier

### Default I2S Pin Configuration

| Signal | GPIO |
|--------|------|
| BCK (Bit Clock) | GPIO26 |
| WS (Word Select/LRCK) | GPIO25 |
| DATA (Serial Data) | GPIO22 |

### UART to External DSP (STM32)

| Signal | GPIO | Description |
|--------|------|-------------|
| TX | GPIO4 | To STM32 USART2 RX (PD6) |
| RX | GPIO5 | From STM32 USART2 TX (PD5) |

GATT commands received via BLE are echoed as `GATT:CTRL:<hex>\r\n` on GPIO4 @ 115200 baud.

## Software Requirements

- ESP-IDF v6.0-beta1 or later
- Python 3.8+

## Building

```bash
# Set up ESP-IDF environment
source ~/esp/esp-idf/export.sh

# Configure target
idf.py set-target esp32

# Build
idf.py build

# Flash
idf.py -p /dev/ttyUSB0 flash

# Monitor serial output
idf.py -p /dev/ttyUSB0 monitor
```

### Creating a Merged Binary

For flashing without ESP-IDF tools:

```bash
idf.py merge-bin
# Creates build/merged-binary.bin
# Flash at offset 0x0
```

## Usage

### Bluetooth Audio (A2DP)

1. Power on the ESP32
2. Search for **"42 Decibels"** in your device's Bluetooth settings
3. Pair and connect
4. Play audio — it streams to the I2S DAC

### DSP Control (BLE GATT)

Use any BLE app (nRF Connect, LightBlue) or the 42 Decibels iOS app:

1. Scan for BLE devices
2. Connect to "42 Decibels"
3. Find the DSP Control Service (UUID: `00000001-1234-5678-9ABC-DEF012345678`)
4. Write commands to the Control characteristic (UUID ends in `...0002`)

## BLE Protocol

### Service UUID

```
DSP Control Service: 00000001-1234-5678-9ABC-DEF012345678
```

### Characteristics

| Characteristic | UUID | Properties | Size |
|----------------|------|------------|------|
| Control Write | `00000002-1234-5678-9ABC-DEF012345678` | Write, Write Without Response | 2 bytes |
| Status Notify | `00000003-1234-5678-9ABC-DEF012345678` | Read, Notify | 4 bytes |
| GalacticStatus | `00000004-1234-5678-9ABC-DEF012345678` | Read, Notify | 7 bytes |
| OTA Credentials | `00000005-1234-5678-9ABC-DEF012345678` | Write | 98 bytes |
| OTA URL | `00000006-1234-5678-9ABC-DEF012345678` | Write | 258 bytes |
| OTA Control | `00000007-1234-5678-9ABC-DEF012345678` | Write | 2 bytes |
| OTA Status | `00000008-1234-5678-9ABC-DEF012345678` | Read, Notify | 8 bytes |

### Command Format

Commands are 2 bytes: `[CMD] [VALUE]`

| CMD | Name | Value | Description |
|-----|------|-------|-------------|
| `0x01` | SET_PRESET | `0x00-0x03` | OFFICE / FULL / NIGHT / SPEECH |
| `0x02` | SET_LOUDNESS | `0x00`/`0x01` | Loudness OFF/ON |
| `0x03` | GET_STATUS | `0x00` | Request status notification |
| `0x04` | SET_MUTE | `0x00`/`0x01` | Unmute/Mute audio |
| `0x05` | SET_AUDIO_DUCK | `0x00`/`0x01` | Audio Duck OFF/ON (-12dB panic) |
| `0x06` | SET_NORMALIZER | `0x00`/`0x01` | Normalizer (DRC) OFF/ON |
| `0x07` | SET_VOLUME | `0x00-0x64` | Volume trim 0–100 (0=mute, 100=full) |
| `0x08` | SET_BYPASS | `0x00`/`0x01` | DSP Bypass OFF/ON (skip EQ, keep safety) |
| `0x09` | SET_BASS_BOOST | `0x00`/`0x01` | Bass Boost OFF/ON (+8dB @ 100Hz) |
| `0x0A` | SET_SINE_TEST | `0x00`/`0x01` | Sine Test OFF/ON (1kHz internal tone) |

### Preset Values

| Value | Preset | Description |
|-------|--------|-------------|
| `0x00` | OFFICE | Mild EQ for background/office listening |
| `0x01` | FULL | Rich bass and treble enhancement |
| `0x02` | NIGHT | Balanced for low volume (volume capped at 60%) |
| `0x03` | SPEECH | Voice clarity for podcasts and calls |

### Status Notification Format (4 bytes)

Format: `[VERSION] [PRESET] [LOUDNESS] [FLAGS]`

| Byte | Description |
|------|-------------|
| 0 | VERSION — Protocol version (0x01) |
| 1 | PRESET — Current preset (0–3) |
| 2 | LOUDNESS — Loudness state (0/1) |
| 3 | FLAGS — Status bitfield (see below) |

**FLAGS bitfield (byte 3):**

| Bit | Mask | Field | Description |
|-----|------|-------|-------------|
| 0 | 0x01 | Limiter | Always 1 (limiter is always active) |
| 1 | 0x02 | Clipping | Clipping detected |
| 2 | 0x04 | Thermal | Thermal warning |
| 3 | 0x08 | Muted | Audio is muted |
| 4 | 0x10 | Duck | Audio Duck active |
| 5 | 0x20 | Normalizer | DRC active |
| 6 | 0x40 | Bypass | DSP Bypass active |

> **Note:** FLAGS byte 3 has its own bit layout — it is **not** a copy of the internal `s_dsp_flags` variable. Correctly remapped since v2.4.3.

### GalacticStatus Format (7 bytes)

Extended status with periodic notifications (every 500ms when subscribed).

Format: `[VER] [PRESET] [FLAGS] [ENERGY] [VOLUME] [BATTERY] [LAST_CONTACT]`

| Byte | Field | Description |
|------|-------|-------------|
| 0 | VER | Protocol version (always 0x42) |
| 1 | PRESET | Current preset (0–3) |
| 2 | FLAGS | Shield status bitfield |
| 3 | ENERGY | Reserved (0–100) |
| 4 | VOLUME | Effective volume level (0–100) |
| 5 | BATTERY | Reserved (0–100) |
| 6 | LAST_CONTACT | Seconds since last BLE communication (0–255) |

**Shield Status FLAGS (byte 2):**

| Bit | Mask | Field | Description |
|-----|------|-------|-------------|
| 0 | 0x01 | Muted | Audio is muted |
| 1 | 0x02 | Audio Duck | Volume reduced (panic mode) |
| 2 | 0x04 | Loudness | Loudness compensation enabled |
| 3 | 0x08 | Normalizer | DRC enabled |
| 4 | 0x10 | **Bypass** | DSP Bypass active (corrected v2.4.3) |
| 5 | 0x20 | **Bass Boost** | Bass Boost enabled (corrected v2.4.3) |

### Example Commands (Hex)

```
0100  - Set preset to OFFICE
0101  - Set preset to FULL
0102  - Set preset to NIGHT (volume capped at 60%)
0103  - Set preset to SPEECH
0200  - Disable loudness
0201  - Enable loudness
0300  - Request status
0400  - Unmute
0401  - Mute
0500  - Audio Duck OFF (normal volume)
0501  - Audio Duck ON (panic mode, -12dB)
0600  - Normalizer OFF
0601  - Normalizer ON
0764  - Set volume to 100%
073C  - Set volume to 60%
0700  - Set volume to 0%
0800  - DSP Bypass OFF (full DSP processing)
0801  - DSP Bypass ON (skip EQ, keep safety)
0900  - Bass Boost OFF
0901  - Bass Boost ON (+8dB @ 100Hz)
0A00  - Sine Test OFF
0A01  - Sine Test ON (1kHz internal tone)
```

### OTA Commands (Write to OTA Control characteristic)

| CMD | Name | Description |
|-----|------|-------------|
| `0x10` | START | Start OTA download process |
| `0x11` | CANCEL | Cancel active OTA |
| `0x12` | REBOOT | Reboot to new firmware |
| `0x13` | GET_VERSION | Get current firmware version |
| `0x14` | ROLLBACK | Rollback to previous firmware |
| `0x15` | VALIDATE | Mark new firmware as valid |

### OTA Status Format (8 bytes)

Format: `[STATE] [ERROR] [PROGRESS] [DL_KB_L] [DL_KB_H] [TOTAL_KB_L] [TOTAL_KB_H] [RSSI]`

| Byte | Field | Description |
|------|-------|-------------|
| 0 | STATE | OTA state (0x00=Idle, 0x05=Downloading, 0x07=Success, 0xFF=Error) |
| 1 | ERROR | Error code (0=none) |
| 2 | PROGRESS | Download progress 0–100% |
| 3-4 | DOWNLOADED_KB | Bytes downloaded (little-endian KB) |
| 5-6 | TOTAL_KB | Total firmware size (little-endian KB) |
| 7 | RSSI | WiFi signal strength (signed dBm) |

## Legacy: ESP32-side DSP (≤ v2.3.1)

Up to and including **v2.3.1**, this firmware contained a full DSP processing chain running on the ESP32 itself (`dsp_processor.c/h`). This is useful as a reference or for standalone use without the STM32 DSP engine.

**Last tag with ESP32-side DSP: [v2.3.1](https://github.com/MYBLtd/ChoticVolt-ESP32_I2S_Master_with_BLE_GATT/releases/tag/v2.3.1)**

The v2.3.1 signal chain was:

```
Input → Pre-gain (-3dB) → HPF (95Hz) → Preset EQ → Loudness → Bass Boost → Normalizer → Limiter → Volume → Duck → Mute → Output
```

DSP components in v2.3.1:

| Component | Description |
|-----------|-------------|
| Pre-gain | -3 dB headroom (always active) |
| High-pass Filter | 2nd-order Butterworth @ 95 Hz |
| Preset EQ | 4-band parametric EQ (OFFICE / FULL / NIGHT / SPEECH) |
| Loudness | Bass +12dB @ 80Hz, treble +6dB @ 12kHz |
| Bass Boost | Low-shelf +8dB @ 100Hz |
| Normalizer | Block DRC — threshold -20dB, ratio 4:1, +6dB makeup |
| Limiter | Peak limiter @ -1 dBFS (3ms attack, 120ms release) |
| Volume | Device-side trim 0–100 |
| Audio Duck | -12dB panic reduction |
| Mute | Final output gate |

> The DSP module was removed in v2.4.0 ("Release v2.4.0: V4 architecture — A2DP sink + BLE GATT dual mode") when DSP processing was migrated to the external STM32 DSP engine.

## Project Structure

```
├── CMakeLists.txt
├── README.md
├── Protocol.md                      # Full BLE protocol specification
├── FSD-DSP-001_ESP32_BLE_GATT.md    # Functional Specification Document
├── partitions_ota.csv               # OTA partition table
├── sdkconfig.defaults
└── main/
    ├── CMakeLists.txt
    ├── main.c                       # Application entry point + A2DP/I2S handling
    ├── ble_gatt_dsp.h/.c            # BLE GATT service + UART relay to STM32
    ├── nvs_settings.h/.c            # Persistent storage (NVS)
    ├── ota_manager.h/.c             # OTA state machine and download logic
    └── wifi_manager.h/.c            # WiFi STA mode for OTA downloads
```

## Configuration

### sdkconfig.defaults

Key configuration options:

```
# Dual-mode Bluetooth (Classic + BLE)
CONFIG_BTDM_CTRL_MODE_BTDM=y
CONFIG_BT_CLASSIC_ENABLED=y
CONFIG_BT_A2DP_ENABLE=y
CONFIG_BT_BLE_ENABLED=y
CONFIG_BT_GATTS_ENABLE=y

# Performance optimization
CONFIG_COMPILER_OPTIMIZATION_PERF=y
```

### Customization

- **Device Name**: Change `BT_DEVICE_NAME` in `main.c`
- **I2S Pins**: Modify `I2S_BCK_PIN`, `I2S_WS_PIN`, `I2S_DATA_PIN` in `main.c`
- **UART DSP Relay**: GPIO4/GPIO5 @ 115200 baud — always enabled when STM32 DSP engine is wired

## Changelog

### v2.4.3 (2026-02-18)
- **Fixed:** Bypass and Bass Boost bits were swapped in GalacticStatus Shield byte (bit 4 was Bass Boost, bit 5 was Bypass — reversed from Protocol.md). Now bit 4 (0x10) = Bypass, bit 5 (0x20) = Bass Boost.
- **Fixed:** Status Notify FLAGS byte (characteristic 0x0003, byte 3) was a raw copy of `s_dsp_flags`. Now correctly remapped to Protocol.md layout: bit 0 = limiter always 1, bit 3 = mute, bit 4 = duck, bit 5 = normalizer, bit 6 = bypass.

### v2.4.2 (2026-02-16)
- **Fixed:** Transient DSP state (duck, mute) was not reflected immediately in BLE status notifications.

### v2.4.1 (2026-02-14)
- **New:** Sine Test mode (command `0x0A`) — generates a 1kHz internal tone for DAC/amp verification
- **Fixed:** Audio path ring buffer with 50ms pre-buffering prevents DMA underrun at startup
- **Fixed:** I2S output stuttering on fast connect/disconnect cycles

### v2.4.0 (2026-02-10)
- **New:** V4 architecture — dual Classic BT A2DP + BLE GATT in same firmware
- **New:** UART echo of BLE GATT commands to external STM32 DSP engine (GPIO4/GPIO5 @ 115200)
- **New:** BT RSSI monitoring and SBC codec quality logging
- **Removed:** `dsp_processor` module — DSP processing migrated to external STM32 DSP engine

### v2.3.0 (2026-01-28)
- **New:** Bass Boost feature (+8dB @ 100Hz) — command `0x09`
- **New:** DSP Bypass command (`0x08`) — skips EQ, keeps safety processing
- **Fixed:** Bypass mode now keeps pre-gain, limiter, volume (prevents hot tracks from clipping)
- **Fixed:** Audio Duck debug log was printing "Bypass" incorrectly
- **Fixed:** Sample rate detection for multi-bit SBC bitmasks (fallback to 44.1kHz)

### v2.2.0 (2026-01-26)
- DSP optimizations with block-based normalizer
- Added initial DSP Bypass mode
- Pre-gain adjusted to -3dB

### v2.1.0 (2026-01-23)
- OTA firmware update support (BLE + WiFi hybrid)
- GalacticStatus periodic notifications every 500ms

### v2.0.0 (2026-01-22)
- Volume control with preset-based caps
- Normalizer/DRC feature
- Audio Duck panic button

### v1.1.0 (2026-01-21)
- Initial release with DSP presets and loudness

## Related Projects

This firmware is one of three projects that form the **42dB audio system**:

| Project | Repository | Role |
|---------|-----------|------|
| **42dB STM32 DSP Engine** | [ChaoticVolt-42dB_STM32_DSP_engine](https://github.com/MYBLtd/ChaoticVolt-42dB_STM32_DSP_engine) | Real-time audio DSP processor |
| **42dB iPhone & Apple Watch App** | [ChaoticVolt-42_Decibels-iPhone-and-WatchOS-app](https://github.com/MYBLtd/ChaoticVolt-42_Decibels-iPhone-and-WatchOS-app) | BLE GATT control interface |
| **ESP32 BLE GATT / A2DP firmware** (this repo) | [ChoticVolt-ESP32_I2S_Master_with_BLE_GATT](https://github.com/MYBLtd/ChoticVolt-ESP32_I2S_Master_with_BLE_GATT) | A2DP Bluetooth sink + GATT relay |

### Version Compatibility

The three projects communicate over a shared **UART/BLE protocol** (command format `GATT:CTRL:<hex>`). When the protocol changes, all three must be updated together:

| Protocol | STM32 DSP Engine | ESP32 Firmware | iPhone/Watch App |
|----------|-----------------|----------------|-----------------|
| v1 | v0.3.0 – v0.5.x | v2.4.1 – v2.4.3 | v1.0.x |

## License

PolyForm Noncommercial 1.0.0 — See [LICENSE](LICENSE). Commercial use requires explicit written permission.

## Author

Robin Kluit

## References

- [Audio EQ Cookbook](https://www.w3.org/2011/audio/audio-eq-cookbook.html) — Robert Bristow-Johnson's biquad filter formulas
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/) — Espressif Systems

## Maintenance

This repo currently does not accept external pull requests. Please use Issues or Discussions for reports and suggestions.
