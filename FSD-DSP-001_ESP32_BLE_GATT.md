# Functional Specification Document

## ESP32 Bluetooth Speaker - DSP Presets + Loudness via BLE GATT

## Version
**Document ID:** FSD-DSP-001
**Status:** Draft (v0.9)
**Date:** 2026-01-20
**GIT Origin:** https://github.com/MYBLtd/ESP32_BT-SPKR-DSP-001_BLE_GATT.git

## Author

Robin Kluit

## 1. Purpose and Scope

This document specifies the functional requierment, behaviou, interfaces and constraints of a Bluetooth audio receiver processing an audio stream and driving a loudspeaker.

The FSD is intended to:

- Define **what the system shall do**, not how it is implemented internally
- Serve as a reference for development, testing and future extensions
- Be understandable by engineers, educators and tecnically inclined users

## 2. System Overview

### 2.1 System Description
The system offers a headless user interface (no buttons/GUI on the speaker) that allows users to use their phone to:

Select presets (e.g., Office, Full, Night, Speech).

Toggle loudness (on/off, optional with strength).

Optionally adjust bass boost and treble in discrete steps.

Audio is streamed via Bluetooth Classic A2DP. Control is via BLE GATT. A dedicated companion app is not required for v1: control should be possible with generic BLE apps (nRF Connect/LightBlue).

### 2.2 Scope (v1)

**In scope:**
- DSP preset engine: vaste EQ-curves per preset
- Loudness overlay (aan/uit)
- Beschermingsketen: high-pass + limiter
- BLE GATT control plane (write + status notify)
- Persistente opslag van settings
- Volume-dependent loudness based on 'speaker volume'
- OTA updates

**Out of scope**
- Room calibration / microfoon-measuring
- Wi-Fi SoftAP UI

### 2.3 Design-principles

A2DP audio in -> decode -> DSP (EQ/presets/loudness/limiter) -> I2S out -> amplifier/driver. 
BLE Controls send discrete settings to DSP without interrupting audio.
Robust: no audio stuttering due to control traffic.

Simple UX: buttons/presets and toggles, no ‘pro audio’ terms needed.

Driver protection first: bass boost should never cause driver/amp clipping or over-excitation.

### 2.4 Assumptions and parameters

Since the exact driver/enclosure may still be unknown, this document uses practical defaults that can be easily tuned later.

Assumptions (default):
- Driver: small full range (± 40-70 mm)
- Enclosure: suboptimal/small volume -> limited real low end
- Sample rate: 44.1 kHz or 48 kHz (DSP must support both)
- Mono or stereo: DSP must support mono; stereo optional

## 3 Functional Requirements

### 3.1 Bluetooth functionality

**FR-1**
The system shall publish it's system name on bluetooth to allow devices to connect to it.

**FR-2**
The system understands IoS bluetooth security and is able to let a IoS device connect to it.

**FR-3**
The system receives an audio stream from the connecte device

**FR-4**
The system porcesses the received audio stream and outputs it using I2S.

**FR-5**
The system processes the received audio stream and outputs it using I2S.

**FR-6**
The system uses a robust mechanism to recover itself from crashes or other malfunctioning.

### 3.2  DSP functionality

**FR-7**
Global DSP headroom (requirement):
Pre-gain: -6 dB (default) to cleanly accommodate EQ boosts + loudness + limiter.

**FR-8**
The system implements at least 3 presets:
	•	OFFICE (office/background)
	•	FULL (full/rich)
	•	NIGHT (low volume / evening)
	•	SPEECH (podcast/speech)

**FR-9**
User can Toggle loudness: with a toggle, make low-volume playback fuller and clearer.
	•	Loudness is on/off.
	•	Loudness overlay is mild and designed for low volume.
	•	Loudness overlay must not make limiter behavior extremely “pumpy”.

Optional (v1.1): Loudness strength 0..2 (mild/normal/strong).

**FR-10** Bass/Treble discrete steps (optional v1.1)
	•	Bass boost level: 0..3
	•	Treble level: 0..2

**FR-11**
Safety: the speaker must not distort or “bottom out” due to overly aggressive bass.

**FR12** Persistent storage
- Last selected preset and loudness state are stored in NVS/flash.
- Write frequency is limited: only save after 1–2 s without changes or on a commit event.

**FR-13** Live parameter updates
- Parameter updates must not cause audible clicks (20–50 ms ramp on gains/filters).
- DSP remains real-time; control tasks must not block audio tasks.

**FR-14**
Latency: control updates feel < 150 ms (best effort).
	
**FR-15**
Stability: BLE connect/disconnect must not interrupt A2DP audio.
	
**FR-16**
CPU budget: DSP chain within real-time budget on ESP32 (no large FFTs in v1).
	
**FR-17**
Memory: no dynamic allocations in the audio callback.



## 4 DSP architecture

### 4.1 Signal chain (v1)
	•	Pre-gain: -6 dB
	•	High-pass filter (HPF): protection against subsonic/unnecessary low end
	•	Preset EQ: fixed filters per preset
	•	Loudness overlay EQ (if enabled)
	•	Limiter: output protection against clipping and driver overdrive
	•	Post-gain: 0 dB (default)

### 4.2  Filter type

Recommended default (implementation freedom allowed): IIR biquad filters:
	•	High-pass (2nd order)
	•	Low-shelf / High-shelf
	•	Peaking (bell) EQ

### 4.3  Limiter behavior (minimum)

Recommended starting values (tunable):
	•	Threshold: -1.0 dBFS
	•	Attack: 2–5 ms
	•	Release: 80–200 ms
	•	Soft-knee: mild (or equivalent)
	
## 8. Preset definition

### 8.1 Global defaults (always active)

| Component | Parameter | Value               |
| --------- | --------- | ------------------- |
| Pre-gain  | Gain      | -6 dB               |
| HPF       | Type      | 2nd-order high-pass |
| HPF       | Fc        | 95 Hz (default)     |
| Limiter   | Threshold | -1.0 dBFS           |
| Limiter   | Attack    | 3 ms                |
| Limiter   | Release   | 120 ms              |

### 8.2 Preset: OFFICE (office/background)

| #    | Type       |      Fc |    Gain | Q / Slope |
| ---- | ---------- | ------: | ------: | --------- |
| 1    | Low-shelf  |  160 Hz | +1.5 dB | S=0.7     |
| 2    | Peaking    |  320 Hz | -1.0 dB | Q=1.0     |
| 3    | Peaking    | 2800 Hz | -1.5 dB | Q=1.0     |
| 4    | High-shelf | 9000 Hz | +0.5 dB | S=0.7     |


### 8.3 Preset: FULL (full/rich)

| #    | Type       |      Fc |    Gain | Q / Slope |
| ---- | ---------- | ------: | ------: | --------- |
| 1    | Low-shelf  |  140 Hz | +4.0 dB | S=0.8     |
| 2    | Peaking    |  420 Hz | -1.5 dB | Q=1.0     |
| 3    | Peaking    | 3200 Hz | +0.7 dB | Q=1.0     |
| 4    | High-shelf | 9500 Hz | +1.5 dB | S=0.7     |

### 8.4 Preset: NIGHT (low volume / evening)

| #    | Type       |      Fc |    Gain | Q / Slope |
| ---- | ---------- | ------: | ------: | --------- |
| 1    | Low-shelf  |  160 Hz | +2.5 dB | S=0.8     |
| 2    | Peaking    |  350 Hz | -1.0 dB | Q=1.0     |
| 3    | Peaking    | 2500 Hz | +1.0 dB | Q=1.0     |
| 4    | High-shelf | 9000 Hz | +1.0 dB | S=0.7     |

### 8.5 Preset: SPEECH (optional)

| #    | Type      |      Fc |    Gain | Q / Slope |
| ---- | --------- | ------: | ------: | --------- |
| 1    | Low-shelf |  170 Hz | -2.0 dB | S=0.8     |
| 2    | Peaking   |  300 Hz | -1.0 dB | Q=1.0     |
| 3    | Peaking   | 3200 Hz | +3.0 dB | Q=1.0     |
| 4    | Peaking   | 7500 Hz | -1.0 dB | Q=2.0     |

## 9. Loudness overlay (v1)

### 9.1 Definition (on/off)

When Loudness is ON, an extra EQ overlay is applied on top of the preset.

| #    | Type       |      Fc |    Gain | Q / Slope |
| ---- | ---------- | ------: | ------: | --------- |
| L1   | Low-shelf  |  140 Hz | +2.5 dB | S=0.8     |
| L2   | High-shelf | 8500 Hz | +1.0 dB | S=0.7     |

### 9.2 Constraints

- Loudness must not make “FULL + Loudness” extremely boomy; overlay is intentionally mild.
- Implementation must apply parameter smoothing when toggling.

Optional: Loudness strength

- 0: off  
- 1: mild (gains above)  
- 2: normal (**+3.5 dB** low-shelf, **+1.5 dB** high-shelf)

## 10. BLE GATT interface

### 10.1 Goal

A generic BLE app must be able to write presets/loudness and read status.

### 10.2 Services & Characteristics

Service UUID: **DSP_CONTROL** (custom 128-bit UUID, assigned by the project)

Characteristics:

- **CONTROL_WRITE**  
  - Properties: Write, Write Without Response  
  - Payload: binary (compact)
- **STATUS_NOTIFY**  
  - Properties: Read, Notify  
  - Payload: binary status snapshot

### 10.3 Control protocol (v1 – 2-byte commands)

Format: **[CMD (1 byte)] [VAL (1 byte)]**

|  CMD | Name         |  VAL | Meaning                              |
| ---: | ------------ | ---: | ------------------------------------ |
| 0x01 | SET_PRESET   | 0..3 | 0=OFFICE, 1=FULL, 2=NIGHT, 3=SPEECH  |
| 0x02 | SET_LOUDNESS |  0/1 | 0=OFF, 1=ON                          |
| 0x03 | GET_STATUS   |    0 | Triggers immediate notify (optional) |

Behavior:

- Each valid write results in a DSP update (with smoothing).
- STATUS_NOTIFY sends (notify) the current state after each change.

### 10.4 Status payload (v1 – 4 bytes)

Format: **[VER][PRESET][LOUDNESS][FLAGS]**

- VER: protocol version (starts at **0x01**)  
- PRESET: **0..3**  
- LOUDNESS: **0/1**  
- FLAGS: bitfield (v1):  
  - bit0 limiter active (always 1)  
  - bit1 clipping detected (optional)  
  - bit2 thermal warning (optional)  
  - other bits reserved=0

## 11. State machine and error handling

### 11.1 BLE connect/disconnect

- On BLE disconnect: audio continues playing; settings remain active.
- On reboot: settings are loaded from NVS; status notify on first connect (read/notify).

### 11.2 Unknown command / value

- Unknown CMD: ignore; send status back unchanged.
- VAL out of range: v1 recommendation: ignore and notify status.

## 12. Persistency

### 12.1 Fields to store

- preset_id (uint8)
- loudness (uint8)
- (optional) bass_level, treble_level
- config_version (uint8)

### 12.2 Write policy

- Do not flash on every BLE write.
- Debounce: write after **1–2 seconds** without new changes.

## 13. Performance and real-time requirements

### 13.1 Audio task priority

- Audio pipeline tasks have highest priority.
- BLE handling must never run in the audio callback.

### 13.2 Parameter smoothing

- On preset change: crossfade/parameter ramp (**20–50 ms**) to prevent clicks.
- Filter coefficient updates: switch in one go with output ramp, or interpolate per block.

