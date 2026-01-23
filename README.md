# ESP32 Bluetooth Speaker with DSP and BLE Control

A Bluetooth audio receiver with real-time DSP processing and BLE GATT control interface for ESP32.

**Version:** 1.2 | **Date:** 2026-01-22

## Features

- **Bluetooth A2DP Sink** - Receive high-quality audio from phones, tablets, and computers
- **Real-time DSP Processing** - EQ presets, loudness enhancement, normalizer, and output limiting
- **BLE GATT Control** - Change DSP settings wirelessly using any generic BLE app or the 42 Decibels iOS app
- **Mute & Audio Duck** - Instant mute and panic button for quick volume reduction
- **Normalizer (DRC)** - Dynamic range compression for late-night listening
- **Volume Control** - Device-side volume trim with preset-based caps
- **GalacticStatus** - Extended status reporting with periodic notifications (2x/sec)
- **Persistent Settings** - Settings survive power cycles via NVS flash storage
- **iOS Compatible** - Secure Simple Pairing (SSP) for seamless iOS pairing

## Hardware Requirements

- ESP32-WROOM-32 or compatible module
- I2S DAC (e.g., MAX98357A, PCM5102)
- Speaker/amplifier

### Default I2S Pin Configuration

| Signal | GPIO |
|--------|------|
| BCK (Bit Clock) | GPIO26 |
| WS (Word Select/LRCK) | GPIO25 |
| DATA (Serial Data) | GPIO22 |

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
2. Search for "42 Decibels" in your device's Bluetooth settings
3. Pair and connect
4. Play audio - it will stream to the speaker

### DSP Control (BLE GATT)

Use any BLE app (nRF Connect, LightBlue) to control DSP settings:

1. Scan for BLE devices
2. Connect to "42 Decibels"
3. Find the DSP Control Service (UUID: `00000001-1234-5678-9ABC-DEF012345678`)
4. Write commands to the Control characteristic

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

### Command Format

Commands are 2 bytes: `[CMD] [VALUE]`

| CMD | Name | Value | Description |
|-----|------|-------|-------------|
| `0x01` | SET_PRESET | `0x00` | OFFICE - Mild EQ for background listening |
| `0x01` | SET_PRESET | `0x01` | FULL - Rich bass and treble enhancement |
| `0x01` | SET_PRESET | `0x02` | NIGHT - Balanced for low volume listening |
| `0x01` | SET_PRESET | `0x03` | SPEECH - Voice clarity for podcasts |
| `0x02` | SET_LOUDNESS | `0x00` | Loudness OFF |
| `0x02` | SET_LOUDNESS | `0x01` | Loudness ON |
| `0x03` | GET_STATUS | `0x00` | Request status notification |
| `0x04` | SET_MUTE | `0x00` | Unmute audio |
| `0x04` | SET_MUTE | `0x01` | Mute audio |
| `0x05` | SET_AUDIO_DUCK | `0x00` | Audio Duck OFF |
| `0x05` | SET_AUDIO_DUCK | `0x01` | Audio Duck ON (reduces volume to ~25%) |
| `0x06` | SET_NORMALIZER | `0x00` | Normalizer OFF |
| `0x06` | SET_NORMALIZER | `0x01` | Normalizer ON (dynamic range compression) |
| `0x07` | SET_VOLUME | `0x00-0x64` | Volume trim (0=mute, 100=full) |

### Status Notification Format (4 bytes)

Format: `[VERSION] [PRESET] [LOUDNESS] [FLAGS]`

| Byte | Description |
|------|-------------|
| VERSION | Protocol version (0x01) |
| PRESET | Current preset (0-3) |
| LOUDNESS | Loudness state (0/1) |
| FLAGS | Status bitfield (see below) |

**FLAGS bitfield:**
- Bit 0: Limiter active (always 1)
- Bit 1: Clipping detected
- Bit 2: Thermal warning
- Bit 3: Muted
- Bit 4: Audio Duck active
- Bit 5: Normalizer active

### GalacticStatus Format (7 bytes)

Extended status with periodic notifications (every 500ms when subscribed).

Format: `[VER] [PRESET] [FLAGS] [ENERGY] [VOLUME] [BATTERY] [LAST_CONTACT]`

| Byte | Field | Description |
|------|-------|-------------|
| 0 | VER | Protocol version (always 0x42) |
| 1 | PRESET | Current preset (0-3) |
| 2 | FLAGS | Shield status bitfield |
| 3 | ENERGY | Reserved (0-100) |
| 4 | VOLUME | Effective volume level (0-100) |
| 5 | BATTERY | Reserved (0-100) |
| 6 | LAST_CONTACT | Seconds since last BLE communication (0-255) |

**Shield Status FLAGS (byte 2):**
- Bit 0 (0x01): Muted
- Bit 1 (0x02): Audio Duck active
- Bit 2 (0x04): Loudness enabled
- Bit 3 (0x08): Normalizer enabled

### Example Commands (Hex)

```
0100  - Set preset to OFFICE
0101  - Set preset to FULL
0102  - Set preset to NIGHT
0103  - Set preset to SPEECH
0200  - Disable loudness
0201  - Enable loudness
0300  - Request status
0400  - Unmute
0401  - Mute
0500  - Audio Duck OFF
0501  - Audio Duck ON (panic button)
0600  - Normalizer OFF
0601  - Normalizer ON
0764  - Set volume to 100%
073C  - Set volume to 60%
0700  - Set volume to 0% (mute)
```

## DSP Architecture

### Signal Chain

```
Input → Pre-gain (-6dB) → HPF (95Hz) → Preset EQ → Loudness EQ → Normalizer → Limiter → Volume Trim → Audio Duck → Mute → Output
```

### Components

1. **Pre-gain**: -6 dB headroom for EQ boosts
2. **High-pass Filter**: 2nd-order Butterworth at 95 Hz (driver protection)
3. **Preset EQ**: 4-band parametric EQ per preset
4. **Loudness Overlay**: 2-band shelf EQ (bass + treble boost)
5. **Normalizer**: Dynamic range compression (threshold -20dB, ratio 4:1, +6dB makeup)
6. **Limiter**: Peak limiter at -1 dBFS (3ms attack, 120ms release)
7. **Volume Trim**: Device-side volume control (0-100, capped by preset/normalizer)
8. **Audio Duck**: Volume reduction to ~25% (-12 dB) when activated
9. **Mute**: Final output gate

### Preset EQ Curves

#### OFFICE (Background/Mild)
| Band | Type | Frequency | Gain | Q/Slope |
|------|------|-----------|------|---------|
| 1 | Low-shelf | 160 Hz | +1.5 dB | S=0.7 |
| 2 | Peaking | 320 Hz | -1.0 dB | Q=1.0 |
| 3 | Peaking | 2800 Hz | -1.5 dB | Q=1.0 |
| 4 | High-shelf | 9000 Hz | +0.5 dB | S=0.7 |

#### FULL (Rich/Enhanced)
| Band | Type | Frequency | Gain | Q/Slope |
|------|------|-----------|------|---------|
| 1 | Low-shelf | 140 Hz | +4.0 dB | S=0.8 |
| 2 | Peaking | 420 Hz | -1.5 dB | Q=1.0 |
| 3 | Peaking | 3200 Hz | +0.7 dB | Q=1.0 |
| 4 | High-shelf | 9500 Hz | +1.5 dB | S=0.7 |

#### NIGHT (Low Volume)
| Band | Type | Frequency | Gain | Q/Slope |
|------|------|-----------|------|---------|
| 1 | Low-shelf | 160 Hz | +2.5 dB | S=0.8 |
| 2 | Peaking | 350 Hz | -1.0 dB | Q=1.0 |
| 3 | Peaking | 2500 Hz | +1.0 dB | Q=1.0 |
| 4 | High-shelf | 9000 Hz | +1.0 dB | S=0.7 |

#### SPEECH (Voice Clarity)
| Band | Type | Frequency | Gain | Q/Slope |
|------|------|-----------|------|---------|
| 1 | Low-shelf | 170 Hz | -2.0 dB | S=0.8 |
| 2 | Peaking | 300 Hz | -1.0 dB | Q=1.0 |
| 3 | Peaking | 3200 Hz | +3.0 dB | Q=1.0 |
| 4 | Peaking | 7500 Hz | -1.0 dB | Q=2.0 |

### Loudness Overlay
| Band | Type | Frequency | Gain | Q/Slope |
|------|------|-----------|------|---------|
| L1 | Low-shelf | 140 Hz | +2.5 dB | S=0.8 |
| L2 | High-shelf | 8500 Hz | +1.0 dB | S=0.7 |

## Project Structure

```
├── CMakeLists.txt
├── README.md
├── FSD-DSP-001_ESP32_BLE_GATT.md    # Functional Specification Document
├── sdkconfig.defaults
└── main/
    ├── CMakeLists.txt
    ├── main.c                       # Application entry point
    ├── dsp_processor.h              # DSP API
    ├── dsp_processor.c              # DSP implementation (biquads, presets, limiter)
    ├── ble_gatt_dsp.h               # BLE GATT service API
    ├── ble_gatt_dsp.c               # BLE GATT implementation
    ├── nvs_settings.h               # Persistent storage API
    └── nvs_settings.c               # NVS implementation with debouncing
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
- **EQ Curves**: Edit `preset_params` array in `dsp_processor.c`
- **Limiter Settings**: Adjust `DSP_LIMITER_*` defines in `dsp_processor.h`

## Functional Requirements (FSD-DSP-001 v1.2)

| ID | Requirement | Status |
|----|-------------|--------|
| FR-1 | Bluetooth device name publishing | ✅ |
| FR-2 | iOS Bluetooth security (SSP) | ✅ |
| FR-3 | A2DP audio stream receiving | ✅ |
| FR-4/5 | I2S audio output | ✅ |
| FR-6 | Watchdog crash recovery | ✅ |
| FR-7 | DSP pre-gain headroom (-6 dB) | ✅ |
| FR-8 | DSP presets (4 presets) | ✅ |
| FR-9 | Loudness toggle | ✅ |
| FR-11 | Safety limiter | ✅ |
| FR-12 | Persistent NVS storage | ✅ |
| FR-13 | Click-free parameter updates | ✅ |
| FR-14 | Control latency < 150ms | ✅ |
| FR-15 | BLE stability (no A2DP interruption) | ✅ |
| FR-16 | Real-time DSP budget | ✅ |
| FR-17 | No dynamic allocation in audio path | ✅ |
| FR-18 | GalacticStatus characteristic (7 bytes) | ✅ |
| FR-19 | Last contact tracking | ✅ |
| FR-20 | Periodic status notifications (500ms) | ✅ |
| FR-21 | Mute function with smooth fade | ✅ |
| FR-22 | Audio Duck (panic button, -12 dB) | ✅ |
| FR-23 | Normalizer (dynamic range compression) | ✅ |
| FR-24 | Volume Control (device trim with caps) | ✅ |

## License

MIT License - See LICENSE file for details.

## Author

Robin Kluit

## Acknowledgments

- Espressif Systems for ESP-IDF
- Audio EQ Cookbook by Robert Bristow-Johnson for biquad filter formulas
