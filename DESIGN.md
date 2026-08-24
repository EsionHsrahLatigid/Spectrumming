# Design

## Source of truth
- Status: Implemented v0.1
- Last refreshed: 2026-08-18
- Primary product surfaces: JUCE plug-in editor, Standalone application shell, staged release metadata, README/build documentation.
- Evidence reviewed: repository root, empty `Source/` and `Tests/`, sibling EHL JUCE projects `DeltaSpine` and `PacketRot`, installed OMX design template, installed EHL brand/plugin UI references, workspace EHL evidence tree at `/Users/2bit/prog/ehl`.

## Brand
- Personality: dark, experimental, digital, harsh, technical, underground, precise, and tool-like.
- Trust signals: stable plug-in identity, deterministic core behavior, compact operational UI, verified staged artifacts, public copy that uses only the public identity `EsionHsrahLatigid`.
- Avoid: chromatic accents, neon cyberpunk, RGB split, fake hardware, waveform logos, decorative glow, oversized branding, copied local logo assets, and public explanation of internal brand rationale.

## Product goals
- Goals: provide a MIDI-driven image-to-spectrum instrument; keep core, plug-in wrapper, UI, and bridge surfaces cleanly separated; produce VST3, Standalone, and AU on macOS from the same CMake contract.
- Non-goals: audio-input effect processing, microphone access, decorative visual branding, bundled fonts or assets outside the shared EHL design module.
- Success signals: C++17 core tests pass independently of JUCE plug-in builds; plug-in targets advertise synth/MIDI-input behavior; staged artifacts include manifest metadata for `jp.ehl.spectrumming` and `Spct`.

## Personas and jobs
- Primary personas: experimental musicians, noise performers, sound designers, and EHL maintainers.
- User jobs: turn image-derived spectral material into playable MIDI-driven sound; scan instrument state quickly; load the plug-in in a DAW or Standalone host; verify release artifacts without reverse-engineering build folders.
- Key contexts of use: DAW sessions, live or rehearsal setups, offline sound design, local release validation.

## Information architecture
- Primary navigation: one compact editor surface without multi-page navigation unless later controls exceed the approved footprint.
- Core routes/screens: header/status, main image/spectrum readout, voice or mapping controls, tone/shape controls, output/safety controls, optional About surface.
- Content hierarchy: current playable state and level/safety feedback first, primary performance controls second, branding and secondary metadata last.

## Design principles
- Principle 1: operational density beats decorative space; every added pixel must support playability, state clarity, or verification.
- Principle 2: monochrome state grammar must remain readable without color and without damaging operational text.
- Tradeoffs: preserve a compact single surface before adding advanced controls; add moderate height only when it improves grouping, labels, or functional data.

## Visual language
- Color: EHL neutral ramp only: `ink #050505`, `low #2A2A2A`, `mid #8A8A86`, `paper #F2F2F0`; logo uses one color.
- Typography: clean bitmap-inspired mono or sans for controls and values; canonical EHL mark from the shared module for compact headers.
- Spacing/layout rhythm: 4 px base grid, 8 px group spacing, dense rows or matrices for related controls.
- Shape/radius/elevation: square or minimally chamfered geometry, flat surfaces, crisp 1 px or grid-aligned rules, no soft elevation.
- Motion: optional functional animation only for live activity; reduced-motion mode must preserve the same state information.
- Imagery/iconography: quantized image and spectrum data is allowed as functional visualization; no waveform, spectrum, speaker, note, or hardware motifs as logo decoration.

## Components
- Existing components to reuse: `juce-ehl-design-module` `EHL::JuceDesign`, including the canonical compact EHL mark and shared editor chrome.
- New/changed components: Spectrumming frame/spectrum readout with scan head, source toolbar, compact five-column parameter matrix, voice/source/output status.
- Variants and states: normal, hover, focus, active/selected, bypass or silent/safe, warning, disabled; state meaning must survive grayscale screenshots.
- Token/component ownership: EHL palette, mark, and shared chrome stay in `juce-ehl-design-module`; product parameters and visualization belong to Spectrumming source.

## Accessibility
- Target standard: practical desktop plug-in accessibility with keyboard focus and high-contrast readable controls.
- Keyboard/focus behavior: editor wants keyboard focus; focus state uses outline or inversion, not color alone.
- Contrast/readability: primary text and values use high contrast; small labels remain clean and undamaged.
- Screen-reader semantics: name controls by parameter purpose and current value where JUCE exposes accessibility metadata.
- Reduced motion and sensory considerations: animation must be functional, bounded, and replaceable by static state changes.

## Responsive behavior
- Supported breakpoints/devices: desktop plug-in and Standalone windows at 100% and host scale factors; approved reference footprint is 640 x 480.
- Layout adaptations: maintain the 4 px grid; compact header uses the shared short mark once; at widths below 640, scale/inset the mark with editor width; above 640, do not let branding dominate.
- Touch/hover differences: pointer hit areas should remain at least 24 x 24 logical px or use larger invisible hit regions.

## Interaction states
- Loading: static shell with disabled controls and explicit initializing status.
- Empty: no MIDI or no voices shows a quiet grid/readout, not decorative filler.
- Error: shape, label, or dither warning pattern; never color-only.
- Success: active event, trigger, or locked state shown with inversion/fill and value readout.
- Disabled: `mid` tone with reduced structure and preserved labels.
- Offline/slow network, if applicable: not applicable; the plug-in must not require network access.

## Content voice
- Tone: terse, technical, public-facing, and release-safe.
- Terminology: `Spectrumming`, `EsionHsrahLatigid`, `MIDI`, `image`, `spectrum`, `voice`, `mapping`, `output`, `safety`.
- Microcopy rules: no internal brand rationale, no marketing-heavy claims, no unexplained abbreviations where a DAW user needs quick state reading.

## Implementation constraints
- Framework/styling system: JUCE 8.0.15, C++17, VST3/Standalone plus AU on Apple, `IS_SYNTH TRUE`, MIDI input enabled, MIDI output disabled, no audio input/microphone permission, GUI bridge app from `Source/bridge/BridgeMain.cpp`.
- Design-token constraints: use the EHL monochrome 8-bit palette and shared module; do not copy shared logo path data into product source.
- Performance constraints: audio/MIDI processing remains bounded, deterministic, allocation-free in realtime code, and independent from UI ownership.
- Compatibility constraints: `EHL_COPY_PLUGIN_AFTER_BUILD` defaults on only for local Apple builds outside CI; release artifacts stage under `artifacts/plugin-release/<platform>/`.
- Test/screenshot expectations: `Tests/CoreTests.cpp` runs without plug-in targets; plug-in release validation runs host load, image/state/MIDI integration, bridge protocol and IPC, artifact manifest, AU metadata checks on Apple, and deterministic editor rendering at 640 x 480.

## Resolved decisions

- Public controls: 20 parameters across trigger/timing, spectrum mapping, and voice/output groups.
- Editor: fixed 640×480 operational surface using the shared EHL chrome and short mark.
- Live input: a separate `Spectrumming Bridge` owns UVC and publishes bounded gray8 frames; the plug-in owns only the neutral consumer and audio-safe exchange.
- Live cadence: the plug-in polls the latest-only transport at 30 Hz as a message-thread/UI work ceiling, not as a required camera frame rate; one second without progress invalidates the preview.
- Bridge lifecycle: closing the window backgrounds the existing capture process, `OPEN BRIDGE` restores that single instance, and only `STOP CAMERA` or `QUIT BRIDGE` releases the device; macOS holds a user-initiated background activity while capture is active.
- Future input SDKs: Syphon, Spout, and nozzle remain bridge-side adapters behind `IFrameSink`; they do not change the plug-in/DSP contract.
