# Audio Path

## Purpose of this document

This file explains the bridge-side audio path in the 42dB platform.

The goal is to show how Bluetooth audio enters the system, what role the bridge plays in that path, and how audio is handed off toward the downstream DSP engine.

## High-level audio flow

At a conceptual level, the current audio path looks like this:

- phone, tablet, or other upstream source
- Bluetooth audio ingress on the bridge
- digital audio handoff over I2S
- downstream DSP processing in `ChaoticVolt-42dB-DSP-Engine`
- DAC / output stage

The bridge therefore acts as the transport-facing audio ingress component rather than the final processing owner.

## Bridge responsibilities in the audio path

This repository is responsible for the bridge-side portion of the audio flow, including:

- receiving Bluetooth audio
- maintaining the audio ingress side of the link
- preparing or forwarding the digital audio stream toward the DSP engine
- preserving stable handoff behavior over I2S
- participating in platform-level coordination where transport continuity matters

The exact implementation details may evolve, but the bridge role remains stable.

## I2S handoff

A key responsibility of the bridge is handing digital audio off toward the DSP engine over I2S.

That means this repository sits at an important boundary:

- upstream: Bluetooth audio transport
- local: transport-facing audio handling and handoff
- downstream: dedicated DSP processing

The main architectural point is that audio processing itself is no longer the central identity of this repository. The bridge prepares and forwards the stream so the DSP engine can own the signal-processing work.

## Real-time expectations

Even though the bridge is not the DSP engine, it still participates in a real-time audio pipeline.

That means the bridge must preserve:

- stable audio ingress behavior
- reliable handoff timing
- predictable I2S behavior
- practical continuity during normal control or state updates

In other words, the bridge still matters to perceived audio quality even if the heavy DSP work happens later in the chain.

## Relationship to Bluetooth ingress

The bridge is the Bluetooth-facing side of the audio pipeline.

That means it is also the layer where transport assumptions, codec behavior, and wireless link realities meet the rest of the platform. It does not control every variable of Bluetooth playback, but it does define how the platform receives and forwards that stream.

## Relationship to the DSP engine

This repository should be read as upstream of `ChaoticVolt-42dB-DSP-Engine`.

Once the stream is handed off over I2S, the DSP engine becomes the owner of:

- real-time signal processing
- preset behavior
- dynamics handling
- DSP-side control application
- downstream audio output toward the DAC stage

That separation should remain clear in the docs.

## Suggested future additions

As the repo matures, this file may later grow to include:

- Bluetooth audio block diagrams
- codec and transport assumptions
- I2S interface details
- startup-order notes
- handoff timing notes
- known transport-side limitations
- debugging notes for audio-ingress failures

For now, this file should stay focused on the architectural role of the bridge in the audio path.
