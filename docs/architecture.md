# Architecture

## Architecture overview

The 42dB platform is built as a small multi-repository system rather than a single all-in-one codebase.

This repository covers the bridge layer: the component that sits between the user-facing control app, Bluetooth audio ingress, and the downstream DSP engine. It exists so transport-facing and protocol-facing responsibilities do not have to live inside the DSP repository itself.

At a conceptual level, the system separates:

- user-facing control
- BLE-facing control handling
- Bluetooth audio ingress and transport-side integration
- bridge-layer forwarding and state coordination
- downstream real-time DSP processing
- output conversion and playback

That separation makes the architecture easier to reason about and easier to evolve over time.

## High-level component layout

The current platform can be understood as a chain of cooperating components:

1. **ChaoticVolt-42dB-Companion-App**
   - user-facing control on iPhone, iPad, and Apple Watch
   - settings, status, and quick controls

2. **ChaoticVolt-BLE-I2S-Bridge**
   - exposes the BLE GATT interface
   - receives Bluetooth audio
   - forwards digital audio over I2S
   - relays control commands toward the DSP engine
   - reflects device state back to BLE clients

3. **ChaoticVolt-42dB-DSP-Engine**
   - receives audio over I2S
   - applies real-time DSP
   - maintains DSP-side state
   - returns status toward the bridge layer

4. **DAC / output stage**
   - receives processed audio
   - handles conversion and downstream playback

## Why the bridge exists

The bridge exists because transport-facing and protocol-facing concerns are different from DSP concerns.

This repository can focus on:

- Bluetooth audio ingress
- BLE GATT exposure
- control translation and forwarding
- state synchronization
- update and persistence glue

That allows the DSP engine to focus on:

- signal processing
- real-time audio behavior
- DSP-side state and control application

This split is one of the key architectural clarifications in the current platform.

## Audio path

At a high level, the audio path looks like this:

- upstream Bluetooth audio source
- bridge-layer Bluetooth ingress
- digital audio handoff over I2S
- downstream DSP engine processing
- DAC / output stage

The bridge is therefore not the final audio-processing owner. It is the transport-facing handoff component between the wireless input side and the DSP side.

## Control path

The control path is separate from the raw audio path.

User interaction begins in `ChaoticVolt-42dB-Companion-App`, which connects over BLE to the bridge. The bridge exposes the BLE GATT interface, validates or translates control messages, and forwards relevant commands toward `ChaoticVolt-42dB-DSP-Engine` over UART or a comparable lower-level control path.

Status can then travel back in the opposite direction so the app remains aligned with device reality.

Conceptually, the control path looks like this:

- companion app
- BLE GATT layer
- bridge logic
- UART or equivalent control link
- DSP engine
- returned state back through the bridge

## Historical note

Earlier iterations of the project may have concentrated more responsibilities in this ESP32-side codebase, including behavior that is now explicitly owned by the DSP engine.

That history matters, but it should not dominate the public identity of the repository.

The current public role of this repository is narrower and cleaner:

- bridge
- relay
- transport-facing integration
- control synchronization

## Architecture principle

The important architectural principle for this repository is this:

**current role belongs in public-facing docs; older implementation history belongs in development notes.**

That is what keeps the repository understandable from the outside.
