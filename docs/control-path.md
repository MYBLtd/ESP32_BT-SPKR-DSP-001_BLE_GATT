# Control Path

## Purpose of this document

This file describes how control data moves through the bridge layer in the 42dB platform.

The bridge is the control-facing component that receives BLE-based input from the companion app, forwards relevant commands toward the DSP engine, and helps keep device state synchronized back toward the user-facing side.

## High-level control flow

At a conceptual level, the control path looks like this:

- `ChaoticVolt-42dB-Companion-App`
- BLE GATT interface on the bridge
- bridge-side validation and forwarding
- UART or equivalent control link
- `ChaoticVolt-42dB-DSP-Engine`
- returned state back through the bridge toward the app

This is one of the core responsibilities of the repository.

## Bridge responsibilities in the control path

The bridge is responsible for the control-facing part of the system, which may include:

- exposing the BLE GATT service
- handling characteristic reads, writes, and notifications
- validating incoming control requests
- translating user-facing control actions into DSP-facing commands
- forwarding commands toward the DSP engine
- receiving returned state or acknowledgements
- reflecting current device state back toward the app
- coordinating settings persistence where needed

## Why BLE lives here

BLE belongs in the bridge layer because it is part of the user-facing control boundary, not the DSP core.

That keeps the architecture cleaner:

- app-side semantics and BLE-facing concerns stay here
- DSP behavior stays in `ChaoticVolt-42dB-DSP-Engine`

This separation allows both layers to evolve without being too tightly coupled.

## UART forwarding

A key bridge role is forwarding control information toward the DSP engine over UART or a similar lower-level channel.

That lower-level control link provides a clean handoff point between:

- BLE-facing control
- bridge-side protocol coordination
- downstream DSP-side control application

The exact message format belongs in the protocol documentation rather than in this file.

## State synchronization

The bridge does not only send commands forward. It also helps keep state coherent across the system.

That means the bridge may need to:

- reflect current DSP-related settings back to BLE clients
- surface device status in a stable way
- prevent the app view from drifting away from actual device behavior
- coordinate notification or polling patterns as needed

This return-path behavior is one of the main reasons the bridge exists as its own component.

## Platform relationship

This repository makes the control path usable and practical from the user-facing side.

Without it, the app would not have a clean BLE-level interface to discover, control, and inspect the downstream DSP-driven system.

That is why this repo sits naturally between:

- `ChaoticVolt-42dB-Companion-App`
- `ChaoticVolt-42dB-DSP-Engine`

## Suggested future additions

As the repo matures, this file may later grow to include:

- control-flow diagrams
- request and response patterns
- notification strategy
- persistence notes
- control ownership boundaries
- edge-case handling notes
- synchronization troubleshooting notes

For now, this file should remain focused on the bridge role in the control path.
