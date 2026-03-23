# Overview

## Summary

ChaoticVolt BLE-I2S Bridge is the transport-facing and control-bridging component of the ChaoticVolt 42dB platform.

It receives Bluetooth audio, forwards digital audio toward the downstream DSP engine over I2S, exposes the BLE GATT control interface used by the companion app, and relays control messages toward `ChaoticVolt-42dB-DSP-Engine`.

This repository is not the DSP engine and it is not the user-facing app. Its value lies in the glue between those layers.

## Role in the 42dB platform

Within the broader 42dB ecosystem, this repository sits between the user-facing control layer and the DSP layer.

At a high level, the platform is split into:

- **ChaoticVolt-42dB-Companion-App** — user-facing control on iPhone, iPad, and Apple Watch
- **ChaoticVolt-BLE-I2S-Bridge** — BLE control bridge and transport-facing integration layer
- **ChaoticVolt-42dB-DSP-Engine** — real-time audio DSP processing
- downstream **DAC / output stage** — conversion and playback

This repository exists so the DSP engine can stay focused on signal processing while the bridge handles transport and protocol-facing responsibilities.

## What this repository does

This repository is intended to contain the bridge-side implementation for the 42dB platform, including:

- Bluetooth audio ingress
- I2S handoff toward the DSP engine
- BLE GATT service exposure
- forwarding control commands toward the DSP engine
- reflecting device state back toward BLE clients
- settings persistence and platform glue
- support for update-related coordination where applicable

## What this repository is not

This repository is not primarily:

- the main DSP implementation
- a complete all-in-one speaker firmware stack
- the user-facing companion app
- a generic ESP32 playground
- the final definition of the entire product platform

Those things may overlap historically or technically, but they are not the current identity of the repository.

## Current scope

At the current stage, the repository reflects an active platform integration component.

That means it combines:

- reusable bridge-layer responsibilities
- practical Bluetooth / BLE / UART / I2S integration
- prototype-stage implementation decisions
- remnants of earlier architectural phases that are being cleaned up into a more stable public role

## Current status

ChaoticVolt BLE-I2S Bridge should be read as an active platform component under continued development.

Its architectural role is already clear, even if hardware choices, transport assumptions, or implementation details continue to evolve over time.

## Relationship to related repositories

This repository makes the most sense when viewed together with:

- **ChaoticVolt-42dB-Companion-App**
- **ChaoticVolt-42dB-DSP-Engine**

Taken together, these repositories describe the user-facing control path, the bridge layer, and the real-time DSP layer of the 42dB platform.
