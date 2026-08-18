# Spectrumming

Spectrumming is an EsionHsrahLatigid image-to-spectrum instrument. It scans a normalized image horizontally; the brightness down each scan column becomes the magnitude of logarithmically spaced additive-synthesis bands. MIDI notes transpose the spectral instrument around a configurable root note.

Version 0.1 supports static images in the plug-in and UVC cameras through the companion **Spectrumming Bridge** app. The plug-in consumes one neutral versioned frame protocol, so future Syphon, Spout, and nozzle adapters can be added to the bridge without putting capture SDKs, GPU contexts, or OS device ownership in the audio plug-in.

## Signal model

- Horizontal position is scan time; `FORWARD`, `REVERSE`, and `PING-PONG` select traversal.
- Image top maps to the highest band and image bottom maps to the lowest band.
- Luma is shaped by `BLACK`, `GAMMA`, and `INVERT`, then interpolated into 128 logarithmic bands.
- Up to eight voices preserve MIDI sample offsets, velocity, note-relative transposition, attack, and release.
- The audio callback performs no file I/O, camera work, locking, logging, or dynamic allocation.

## Controls

| Group | Parameters | Purpose |
| --- | --- | --- |
| Trigger | `TRIGGER`, `CLOCK`, `DURATION`, `LENGTH`, `DIRECTION`, `CYCLE`, `START` | Auto/MIDI triggering, free or host timing, traversal and phase |
| Spectrum | `LOW HZ`, `HIGH HZ`, `GAMMA`, `BLACK`, `INVERT`, `FREQ SMOOTH`, `FRAME SMOOTH` | Frequency mapping and brightness shaping |
| Voice/output | `ROOT`, `ATTACK`, `RELEASE`, `WIDTH`, `OUTPUT DB`, `MUTE` | MIDI reference, envelope, stereo spread, gain and silence |

The fixed 640×480 editor also provides `IMAGE`, `CAMERA`, `LOAD`, `OPEN BRIDGE`, and `FREEZE` source commands, a quantized frame view, scan head, voice count, source status, and output meter.

## Visual-source boundary

`Source/bridge/BridgeFrame.h` is the stable frame ABI. Producers submit validated frames through `IFrameSink`; platform adapters normalize them to bounded `gray8` frames before `SharedFrameFile` publishes the latest frame. The plug-in polls that transport on the message thread and crosses into audio through a fixed-capacity lock-free exchange.

Current and planned adapters:

| Adapter | Process/platform | Status |
| --- | --- | --- |
| Static image | Plug-in / all supported hosts | Implemented |
| UVC | Bridge / macOS and Windows | Implemented |
| Syphon | Bridge / macOS | Extension point |
| Spout | Bridge / Windows | Extension point |
| nozzle | Bridge adapter | Extension point |

## Build

Fast core tests:

```sh
cmake --preset engine-debug
cmake --build --preset engine-debug --parallel
ctest --preset engine-debug --output-on-failure
```

Full plug-in build with local checkouts:

```sh
cmake --preset plugin-release \
  -DEHL_JUCE_SOURCE_DIR=/absolute/path/to/JUCE \
  -DEHL_JUCE_DESIGN_MODULE_SOURCE_DIR=/absolute/path/to/juce-ehl-design-module \
  -DEHL_COPY_PLUGIN_AFTER_BUILD=OFF
cmake --build --preset plugin-release --parallel
ctest --preset plugin-release --output-on-failure
```

Products are staged under `artifacts/plugin-release/<platform>/`, including the companion bridge on macOS and Windows.

## Identity

- Manufacturer: `EsionHsrahLatigid`
- Manufacturer code: `EHL_`
- Bundle ID: `jp.ehl.spectrumming`
- Plug-in code: `Spct`
- Formats: VST3, Standalone, AU on macOS
- Type: synth/instrument, MIDI input enabled, no audio input

## Design

Spectrumming uses the shared EHL monochrome 8-bit JUCE UI system. The canonical compact EHL mark is rendered by the pinned `juce-ehl-design-module` submodule; no product-local logo copy is used. See [DESIGN.md](DESIGN.md) for the UI contract and [BUILD.md](BUILD.md) for build and test details.

## License and dependencies

Spectrumming source is MIT licensed; see [LICENSE](LICENSE). Builds use JUCE 8.0.15 under the applicable JUCE license and the MIT-licensed `juce-ehl-design-module`. No capture SDK is linked into the plug-in in v0.1.
