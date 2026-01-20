# ESP32 Bluetooth Speaker with DSP and BLE Control

A Bluetooth audio receiver with real-time DSP processing and BLE GATT control interface for ESP32.

## Features

- **Bluetooth A2DP Sink** - Receive high-quality audio from phones, tablets, and computers
- **Real-time DSP Processing** - EQ presets, loudness enhancement, and output limiting
- **BLE GATT Control** - Change DSP settings wirelessly using any generic BLE app
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
2. Search for "ESP32 Speaker" in your device's Bluetooth settings
3. Pair and connect
4. Play audio - it will stream to the speaker

### DSP Control (BLE GATT)

Use any BLE app (nRF Connect, LightBlue) to control DSP settings:

1. Scan for BLE devices
2. Connect to "ESP32 Speaker"
3. Find the DSP Control Service (UUID: `00000001-1234-5678-9ABC-DEF012345678`)
4. Write commands to the Control characteristic

## BLE Protocol

### Service UUID

```
DSP Control Service: 00000001-1234-5678-9ABC-DEF012345678
```

### Characteristics

| Characteristic | UUID | Properties |
|----------------|------|------------|
| Control Write | `00000002-1234-5678-...` | Write, Write Without Response |
| Status Notify | `00000003-1234-5678-...` | Read, Notify |

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

### Status Notification Format

4 bytes: `[VERSION] [PRESET] [LOUDNESS] [FLAGS]`

| Byte | Description |
|------|-------------|
| VERSION | Protocol version (0x01) |
| PRESET | Current preset (0-3) |
| LOUDNESS | Loudness state (0/1) |
| FLAGS | Bit 0: Limiter active, Bit 1: Clipping detected |

### Example Commands (Hex)

```
0100  - Set preset to OFFICE
0101  - Set preset to FULL
0102  - Set preset to NIGHT
0103  - Set preset to SPEECH
0200  - Disable loudness
0201  - Enable loudness
0300  - Request status
```

## DSP Architecture

### Signal Chain

```
Input → Pre-gain (-6dB) → HPF (95Hz) → Preset EQ → Loudness EQ → Limiter → Output
```

### Components

1. **Pre-gain**: -6 dB headroom for EQ boosts
2. **High-pass Filter**: 2nd-order Butterworth at 95 Hz (driver protection)
3. **Preset EQ**: 4-band parametric EQ per preset
4. **Loudness Overlay**: 2-band shelf EQ (bass + treble boost)
5. **Limiter**: Peak limiter at -1 dBFS (3ms attack, 120ms release)

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

## Functional Requirements (FSD-DSP-001)

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

## License

MIT License - See LICENSE file for details.

## Author

Robin Kluit

## Acknowledgments

- Espressif Systems for ESP-IDF
- Audio EQ Cookbook by Robert Bristow-Johnson for biquad filter formulas
