# Protocol

**Version:** 2.1  
**Date:** 2026-01-23

## Overview

This document describes the BLE GATT protocol used by **ChaoticVolt-BLE-I2S-Bridge** within the ChaoticVolt 42dB platform.

In the current architecture, the bridge layer exposes the BLE interface, receives control commands from **ChaoticVolt-42dB-Companion-App**, and relays relevant control data toward **ChaoticVolt-42dB-DSP-Engine**. Status information can then flow back in the opposite direction so the companion app stays aligned with device state.

This file is the canonical reference for:

- BLE service and characteristic UUIDs
- command IDs and payload formats
- status packet structures
- OTA-related message definitions
- bridge-facing protocol behavior

For architectural context, see:

- `README.md`
- `docs/overview.md`
- `docs/architecture.md`
- `docs/control-path.md`

## Service UUID

```text
DSP Control Service: 00000001-1234-5678-9ABC-DEF012345678
```

## Main characteristics

### CONTROL_WRITE

- **UUID:** `00000002-1234-5678-9ABC-DEF012345678`
- **Properties:** Write, Write Without Response
- **Size:** 2 bytes

All commands are sent as two-byte packets:

```text
[COMMAND_TYPE, VALUE]
```

### STATUS_NOTIFY

- **UUID:** `00000003-1234-5678-9ABC-DEF012345678`
- **Properties:** Read, Notify
- **Size:** 4 bytes

This characteristic provides a compact status packet for quick state reflection.

### GALACTIC_STATUS

- **UUID:** `00000004-1234-5678-9ABC-DEF012345678`
- **Properties:** Read, Notify
- **Size:** 7 bytes

This characteristic provides a broader status snapshot and is intended for richer app-side state visibility.

## Control commands

| Command | Byte 0 | Byte 1 | Description |
| --- | --- | --- | --- |
| Set Preset | `0x01` | `0x00-0x03` | Change DSP preset |
| Set Loudness | `0x02` | `0x00-0x01` | Enable or disable loudness compensation |
| Request Status | `0x03` | `0x00` | Request a full status update |
| Set Mute | `0x04` | `0x00-0x01` | Mute or unmute audio |
| Set Audio Duck | `0x05` | `0x00-0x01` | Enable or disable audio duck |
| Set Normalizer | `0x06` | `0x00-0x01` | Enable or disable normalizer / DRC |
| Set Volume | `0x07` | `0x00-0x64` | Set volume trim from 0 to 100 |

## Preset values

| Value | Preset | Description |
| --- | --- | --- |
| `0x00` | OFFICE | Balanced sound for desk or office listening |
| `0x01` | FULL | Full-range voicing with more bass presence |
| `0x02` | NIGHT | Balanced for lower-volume listening |
| `0x03` | SPEECH | Optimized for voice-focused content |

## Command examples

```text
0x01 0x02   Set preset to NIGHT
0x02 0x01   Enable loudness compensation
0x04 0x01   Mute audio
0x04 0x00   Unmute audio
0x05 0x01   Enable audio duck
0x05 0x00   Disable audio duck
0x06 0x01   Enable normalizer
0x06 0x00   Disable normalizer
0x07 0x64   Set volume to 100%
0x07 0x3C   Set volume to 60%
0x07 0x00   Set volume to 0%
```

## STATUS_NOTIFY

### Packet format

```text
[VERSION][PRESET][LOUDNESS][FLAGS]
   0        1        2        3
```

| Byte | Field | Type | Description |
| --- | --- | --- | --- |
| 0 | VERSION | `uint8` | Protocol version (`0x01`) |
| 1 | PRESET | `uint8` | Current preset (`0x00-0x03`) |
| 2 | LOUDNESS | `uint8` | Loudness state (`0x00` or `0x01`) |
| 3 | FLAGS | bitfield | Compact status flags |

### FLAGS bitfield

| Bit | Mask | Field | Description |
| --- | --- | --- | --- |
| 0 | `0x01` | Limiter Active | Always 1 if limiter is considered active |
| 1 | `0x02` | Clipping | Optional clipping indication |
| 2 | `0x04` | Thermal Warning | Optional thermal warning indication |
| 3 | `0x08` | Muted | Audio is muted |
| 4 | `0x10` | Audio Duck | Audio duck is enabled |
| 5 | `0x20` | Normalizer | Normalizer / DRC is enabled |
| 6-7 | — | Reserved | Future use |

## GALACTIC_STATUS

### Packet format

```text
[VER][PRESET][FLAGS][ENERGY][VOLUME][BATTERY][LAST_CONTACT]
  0     1       2      3       4       5          6
```

| Byte | Field | Type | Description |
| --- | --- | --- | --- |
| 0 | VER | `uint8` | Protocol version (expected `0x42`) |
| 1 | PRESET | `uint8` | Active preset |
| 2 | FLAGS | bitfield | Shield status flags |
| 3 | ENERGY | `uint8` | Reserved (`0-100`) |
| 4 | VOLUME | `uint8` | Effective volume level (`0-100`) |
| 5 | BATTERY | `uint8` | Battery placeholder (`0-100`) |
| 6 | LAST_CONTACT | `uint8` | Seconds since last BLE communication |

### Shield status bitfield

| Bit | Mask | Field | Description |
| --- | --- | --- | --- |
| 0 | `0x01` | Muted | Audio is muted |
| 1 | `0x02` | Audio Duck | Volume reduction is active |
| 2 | `0x04` | Loudness | Loudness compensation is enabled |
| 3 | `0x08` | Normalizer | Dynamic range compression is enabled |
| 4-7 | — | Reserved | Future use |

### Last contact behavior

The `LAST_CONTACT` field shows the number of seconds since the last BLE communication event.

Useful interpretation:

- `0` = just communicated
- `1-5` = very recent
- `30+` = getting stale
- `255` = maximum / very old

Communication may include:

- incoming writes
- incoming reads
- outgoing notifications

If `GALACTIC_STATUS` is being notified every 500 ms, this value should usually stay at `0x00` or `0x01`.

### Example packet

```text
42 01 06 64 50 64 00
```

Interpretation:

- Version = `0x42`
- Preset = `0x01` (`FULL`)
- Flags = `0x06`
  - Muted = no
  - Audio Duck = yes
  - Loudness = yes
  - Normalizer = no
- Energy = 100
- Volume = 50
- Battery = 100
- Last Contact = 0 seconds

## Behavioral notes

### Audio Duck

When audio duck is enabled (`0x05 0x01`), the intended behavior is:

1. apply roughly `-12 dB` gain reduction
2. use a smooth transition
3. set the relevant status bit
4. update and notify `GALACTIC_STATUS`

When disabled (`0x05 0x00`), the intended behavior is:

1. restore normal gain
2. use a smooth transition
3. clear the relevant status bit
4. update and notify `GALACTIC_STATUS`

### Normalizer / DRC

When normalizer is enabled (`0x06 0x01`), the intended behavior is:

1. enable dynamic range compression in the downstream DSP path
2. reflect the feature state in status
3. update and notify `GALACTIC_STATUS`

Reference settings currently described in the legacy protocol notes:

- threshold: `-20 dB`
- ratio: `4:1`
- attack: `7 ms`
- release: `150 ms`
- makeup gain: `+6 dB`

If these settings become implementation-dependent, keep this file focused on protocol-facing expectations and move deeper tuning detail into engineering notes.

### Mute

When mute is enabled (`0x04 0x01`), the intended behavior is:

1. apply a smooth fade to zero
2. reflect mute in status
3. update and notify `GALACTIC_STATUS`

### Volume control

When volume is set (`0x07 0xNN`), the intended behavior is:

1. accept values from `0` to `100`
2. apply any preset-dependent or headroom-dependent caps
3. use a smooth transition
4. update effective volume reporting in `GALACTIC_STATUS`

Legacy notes currently describe these example mappings:

- `100` -> `0 dB`
- `80` -> `-6 dB`
- `60` -> `-12 dB`
- `40` -> `-20 dB`
- `20` -> `-35 dB`
- `0` -> mute

If the effective volume model changes later, update this document and the companion app together.

## Characteristic summary

| UUID | Name | Properties | Purpose |
| --- | --- | --- | --- |
| `0x0002` | Control Write | Write, Write Without Response | Send commands to the device |
| `0x0003` | Status Notify | Read, Notify | Receive compact status updates |
| `0x0004` | Galactic Status | Read, Notify | Receive richer status snapshots |
| `0x0005` | OTA Credentials | Write | Provide Wi-Fi credentials for OTA |
| `0x0006` | OTA URL | Write | Provide firmware download URL |
| `0x0007` | OTA Control | Write | Send OTA control commands |
| `0x0008` | OTA Status | Read, Notify | Receive OTA progress updates |

## OTA overview

The bridge supports an OTA flow using BLE for provisioning and control, with Wi-Fi used for actual firmware retrieval.

Typical OTA sequence:

1. write Wi-Fi credentials
2. write firmware URL
3. start OTA
4. monitor OTA status notifications
5. reboot into new firmware
6. validate or rollback as needed

## OTA Credentials

- **UUID:** `00000005-1234-5678-9ABC-DEF012345678`
- **Properties:** Write
- **Size:** Up to 98 bytes

### Format

```text
[SSID (max 32 bytes)] [0x00 separator] [PASSWORD (max 64 bytes)]
```

### Example

```text
MyWiFisecret123
```

## OTA URL

- **UUID:** `00000006-1234-5678-9ABC-DEF012345678`
- **Properties:** Write
- **Size:** Up to 258 bytes

### Format

```text
[LENGTH_L] [LENGTH_H] [URL bytes...]
```

Or a null-terminated URL string, depending on implementation handling.

## OTA Control

- **UUID:** `00000007-1234-5678-9ABC-DEF012345678`
- **Properties:** Write
- **Size:** 2 bytes

### Format

```text
[CMD] [PARAM]
```

### OTA commands

| CMD | Name | Param | Description |
| --- | --- | --- | --- |
| `0x10` | START | `0x00` | Start OTA download |
| `0x11` | CANCEL | `0x00` | Cancel OTA |
| `0x12` | REBOOT | `0x00` | Reboot to apply new firmware |
| `0x13` | GET_VERSION | `0x00` | Request firmware version |
| `0x14` | ROLLBACK | `0x00` | Roll back to previous firmware |
| `0x15` | VALIDATE | `0x00` | Mark new firmware as valid |

## OTA Status

- **UUID:** `00000008-1234-5678-9ABC-DEF012345678`
- **Properties:** Read, Notify
- **Size:** 8 bytes

### Packet format

```text
[STATE][ERROR][PROGRESS][DL_KB_L][DL_KB_H][TOTAL_KB_L][TOTAL_KB_H][RSSI]
   0      1       2         3        4         5           6        7
```

| Byte | Field | Type | Description |
| --- | --- | --- | --- |
| 0 | STATE | `uint8` | Current OTA state |
| 1 | ERROR | `uint8` | Error code (`0x00` = no error) |
| 2 | PROGRESS | `uint8` | Download progress (`0-100`) |
| 3-4 | DOWNLOADED_KB | `uint16` | Downloaded size in KB, little-endian |
| 5-6 | TOTAL_KB | `uint16` | Total image size in KB, little-endian |
| 7 | RSSI | `int8` | Wi-Fi signal strength in dBm |

### OTA states

| Value | State | Description |
| --- | --- | --- |
| `0x00` | IDLE | Ready, no OTA in progress |
| `0x01` | CREDS_RECEIVED | Wi-Fi credentials received |
| `0x02` | URL_RECEIVED | Firmware URL received |
| `0x03` | WIFI_CONNECTING | Connecting to Wi-Fi |
| `0x04` | WIFI_CONNECTED | Wi-Fi connected |
| `0x05` | DOWNLOADING | Download in progress |
| `0x06` | VERIFYING | Verifying firmware |
| `0x07` | SUCCESS | OTA complete, ready for reboot |
| `0x08` | PENDING_VERIFY | New firmware booted, awaiting validation |
| `0xFF` | ERROR | Error condition, see ERROR byte |

### OTA error codes

| Value | Error | Description |
| --- | --- | --- |
| `0x00` | NONE | No error |
| `0x01` | WIFI_CONNECT | Wi-Fi connection failed |
| `0x02` | HTTP_CONNECT | HTTP connection failed |
| `0x03` | HTTP_RESPONSE | HTTP error response |
| `0x04` | DOWNLOAD | Download failed |
| `0x05` | VERIFY | Verification failed |
| `0x06` | WRITE | Flash write failed |
| `0x07` | NO_CREDS | No credentials provided |
| `0x08` | NO_URL | No URL provided |
| `0x09` | INVALID_IMAGE | Invalid firmware image |
| `0x0A` | CANCELLED | OTA cancelled |
| `0x0B` | ROLLBACK_FAILED | Rollback failed |

### OTA example packet

```text
05 00 45 C8 00 20 03 D2
```

Interpretation:

- State = `DOWNLOADING`
- Error = none
- Progress = `69%`
- Downloaded = `200 KB`
- Total = `800 KB`
- RSSI = `-46 dBm`

## Quick reference

### DSP-facing commands via CONTROL_WRITE

```text
0100  Set preset to OFFICE
0101  Set preset to FULL
0102  Set preset to NIGHT
0103  Set preset to SPEECH
0200  Disable loudness
0201  Enable loudness
0300  Request status
0400  Unmute
0401  Mute
0500  Disable audio duck
0501  Enable audio duck
0600  Disable normalizer
0601  Enable normalizer
0764  Set volume to 100%
073C  Set volume to 60%
0700  Set volume to 0%
```

### OTA commands via OTA Control

```text
1000  Start OTA download
1100  Cancel OTA
1200  Reboot to new firmware
1300  Get firmware version
1400  Roll back to previous firmware
1500  Validate new firmware
```

## Maintenance note

This file should stay protocol-facing.

Good candidates for inclusion:

- UUIDs
- command IDs
- payload shapes
- flag meanings
- OTA message definitions
- protocol-facing behavioral expectations

Better placed elsewhere:

- platform marketing
- broad architecture explanation
- low-level implementation notes
- long historical repo evolution
