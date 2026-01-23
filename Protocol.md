# 42 Decibels BLE Protocol Documentation

**Version:** 2.0
**Date:** 2026-01-23

## Overview

This document describes the BLE GATT protocol for controlling the ESP32 Bluetooth Speaker with DSP.

---

## Service UUID

```
DSP Control Service: 00000001-1234-5678-9ABC-DEF012345678
```

---

## Control Commands (Characteristic: 0x0002)

**UUID:** `00000002-1234-5678-9ABC-DEF012345678`
**Properties:** Write, Write Without Response
**Size:** 2 bytes

All commands are sent as 2-byte packets: `[COMMAND_TYPE, VALUE]`

### Command Types

| Command | Byte 0 | Byte 1 | Description |
|---------|--------|--------|-------------|
| **Set Preset** | `0x01` | `0x00-0x03` | Change DSP preset |
| **Set Loudness** | `0x02` | `0x00-0x01` | Enable/disable loudness compensation |
| **Request Status** | `0x03` | `0x00` | Request full status update |
| **Set Mute** | `0x04` | `0x00-0x01` | Mute/unmute audio completely |
| **Set Audio Duck** | `0x05` | `0x00-0x01` | Enable/disable Audio Duck (volume reduction) |
| **Set Normalizer** | `0x06` | `0x00-0x01` | Enable/disable Normalizer/DRC |
| **Set Volume** | `0x07` | `0x00-0x64` | Set volume trim (0=mute, 100=full) |

### Preset Values (for Command 0x01)

| Value | Preset | Description |
|-------|--------|-------------|
| `0x00` | OFFICE | Balanced sound for office/desk use |
| `0x01` | FULL | Full-range audio with enhanced bass |
| `0x02` | NIGHT | Balanced for low volume listening |
| `0x03` | SPEECH | Optimized for voice content |

### Command Examples

```
// Set preset to NIGHT
0x01 0x02

// Enable loudness compensation
0x02 0x01

// Mute audio
0x04 0x01

// Unmute audio
0x04 0x00

// Enable Audio Duck (reduce volume to ~25%)
0x05 0x01

// Disable Audio Duck (restore normal volume)
0x05 0x00

// Enable Normalizer (dynamic range compression)
0x06 0x01

// Disable Normalizer
0x06 0x00

// Set volume to 100% (full)
0x07 0x64

// Set volume to 60%
0x07 0x3C

// Set volume to 0% (mute)
0x07 0x00
```

---

## Status Responses (Characteristic: 0x0003)

**UUID:** `00000003-1234-5678-9ABC-DEF012345678`
**Properties:** Read, Notify
**Size:** 4 bytes

The device sends status updates via notifications on this characteristic after each command.

### Response Format

```
[VERSION][PRESET][LOUDNESS][FLAGS]
   0        1        2        3
```

| Byte | Field    | Type     | Description              |
|------|----------|----------|--------------------------|
| 0    | VERSION  | `uint8`  | Protocol version (0x01)  |
| 1    | PRESET   | `uint8`  | Current preset (0-3)     |
| 2    | LOUDNESS | `uint8`  | Loudness state (0/1)     |
| 3    | FLAGS    | bitfield | Status flags             |

### FLAGS Bitfield (Byte 3)

| Bit | Mask | Field           | Description                    |
|-----|------|-----------------|--------------------------------|
| 0   | 0x01 | Limiter Active  | Always 1 (limiter is on)       |
| 1   | 0x02 | Clipping        | Clipping detected (optional)   |
| 2   | 0x04 | Thermal Warning | Thermal warning (optional)     |
| 3   | 0x08 | Muted           | Audio is muted                 |
| 4   | 0x10 | Audio Duck      | Volume reduced (panic mode)    |
| 5   | 0x20 | Normalizer      | DRC enabled                    |
| 6-7 | -    | Reserved        | Future use                     |

---

## Galactic Status (Characteristic: 0x0004)

**UUID:** `00000004-1234-5678-9ABC-DEF012345678`
**Properties:** Read, Notify
**Size:** 7 bytes

This characteristic provides a comprehensive status snapshot. It is automatically notified every 500ms when notifications are enabled.

### Packet Format

```
[VER][PRESET][FLAGS][ENERGY][VOLUME][BATTERY][LAST_CONTACT]
  0     1       2      3       4       5          6
```

### Byte Descriptions

| Byte | Field        | Type     | Description                          |
|------|--------------|----------|--------------------------------------|
| 0    | VER          | `uint8`  | Protocol version (always `0x42`)     |
| 1    | PRESET       | `uint8`  | Active preset (0-3)                  |
| 2    | FLAGS        | bitfield | Shield status flags (see below)      |
| 3    | ENERGY       | `uint8`  | Reserved (0-100)                     |
| 4    | VOLUME       | `uint8`  | Effective volume level (0-100)       |
| 5    | BATTERY      | `uint8`  | Battery percentage placeholder (0-100)|
| 6    | LAST_CONTACT | `uint8`  | Seconds since last BLE communication |

### Shield Status Bitfield (Byte 2)

| Bit | Mask   | Field      | Description                       |
|-----|--------|------------|-----------------------------------|
| 0   | `0x01` | Muted      | Audio is muted                    |
| 1   | `0x02` | Audio Duck | Volume reduced (panic mode)       |
| 2   | `0x04` | Loudness   | Loudness compensation enabled     |
| 3   | `0x08` | Normalizer | Dynamic range compression enabled |
| 4-7 | -      | Reserved   | Future use                        |

### Last Contact Behavior

The `LAST_CONTACT` field (byte 6) shows seconds since the last BLE communication:

- **0 seconds** = Just communicated
- **1-5 seconds** = Very recent
- **30+ seconds** = Getting stale
- **255 seconds** = Maximum (>4 minutes)

Communication includes:
- Incoming commands (writes)
- Incoming reads
- Outgoing notifications (status updates)

Since GalacticStatus is notified every 500ms, this value should typically be `0x00` or `0x01`.

### Example Galactic Status

```
42 01 06 64 50 64 00
```

Interpretation:
- Protocol Version: `0x42` ✓
- Preset: `0x01` (FULL)
- Shield Status: `0x06` (bits 1 and 2 set)
  - Muted: NO
  - Audio Duck: YES (volume reduced)
  - Loudness: YES
  - Normalizer: NO
- Energy Core: 100%
- Volume: 50%
- Battery: 100%
- Last Contact: 0 seconds ago

---

## Implementation Details

### Audio Duck (FR-22)

When Audio Duck is enabled (`0x05 0x01`):
1. Apply -12 dB gain reduction (~25% volume)
2. Use smooth gain transition (no clicks)
3. Set bit 1 in Shield Status byte
4. Update and notify GalacticStatus

When Audio Duck is disabled (`0x05 0x00`):
1. Restore normal gain (0 dB)
2. Use smooth gain transition
3. Clear bit 1 in Shield Status byte
4. Update and notify GalacticStatus

### Normalizer/DRC (FR-23)

When Normalizer is enabled (`0x06 0x01`):
1. Enable dynamic range compression in DSP chain
2. Settings:
   - Threshold: -20 dB
   - Ratio: 4:1
   - Attack: 7 ms
   - Release: 150 ms
   - Makeup gain: +6 dB
3. Set bit 3 in Shield Status byte
4. Update and notify GalacticStatus

### Mute (FR-21)

When Mute is enabled (`0x04 0x01`):
1. Apply smooth gain fade to 0
2. Set bit 0 in Shield Status byte
3. Update and notify GalacticStatus

### Volume Control (FR-24)

When Volume is set (`0x07 0xNN`):
1. Accept value 0-100 (0x00-0x64)
2. Apply volume cap based on preset:
   - OFFICE/FULL/SPEECH: cap = 100
   - NIGHT: cap = 60
3. If Normalizer is enabled, reduce cap by ~20% for headroom
4. Apply smooth gain transition (no clicks)
5. Update GalacticStatus byte 4 with effective volume
6. Volume to dB mapping:
   - 100 → 0 dB
   - 80 → -6 dB
   - 60 → -12 dB
   - 40 → -20 dB
   - 20 → -35 dB
   - 0 → mute

---

## Characteristic Summary

| UUID   | Name           | Properties                  | Purpose                          |
|--------|----------------|-----------------------------|---------------------------------|
| 0x0002 | Control Write  | Write, Write Without Response | Send commands to device        |
| 0x0003 | Status Notify  | Read, Notify                | Receive simple status updates   |
| 0x0004 | Galactic Status| Read, Notify                | Receive comprehensive status    |

---

## Quick Reference

### All Commands (Hex)

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
0500  - Disable Audio Duck
0501  - Enable Audio Duck
0600  - Disable Normalizer
0601  - Enable Normalizer
0764  - Set volume to 100%
073C  - Set volume to 60%
0700  - Set volume to 0% (mute)
```

---

## Testing Checklist

- [x] Mute silences audio completely
- [x] Mute uses smooth fade (no clicks)
- [x] Audio Duck reduces volume to ~25%
- [x] Audio Duck uses smooth transition
- [x] Normalizer applies compression
- [x] Galactic Status bit 0 reflects Mute state
- [x] Galactic Status bit 1 reflects Audio Duck state
- [x] Galactic Status bit 2 reflects Loudness state
- [x] Galactic Status bit 3 reflects Normalizer state
- [x] Last Contact resets on notifications
- [x] Last Contact shows 0-1 during active connection
- [x] All features work independently
- [ ] Volume trim 0-100 adjusts audio level
- [ ] Volume uses smooth transition (no clicks)
- [ ] NIGHT preset caps volume at 60%
- [ ] Normalizer reduces volume cap for headroom
- [ ] Galactic Status byte 4 reflects effective volume

---

*Last Updated: 2026-01-23 (v2.0)*
