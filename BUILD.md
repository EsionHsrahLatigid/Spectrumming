# Spectrumming Build Notes

Spectrumming follows the EHL JUCE CMake workflow.

## Presets

- `engine-debug`: configures the C++17 `Source/core/SpectrummingCore.cpp` and `Tests/CoreTests.cpp` lane without JUCE plug-in targets.
- `plugin-release`: configures JUCE 8.0.15, VST3, Standalone, AU on macOS, the UVC Bridge, integration and host-load tests, bridge/IPC tests, and staged products.

## Local Dependencies

Use local checkouts to avoid network fetches:

```sh
cmake --preset plugin-release \
  -DEHL_JUCE_SOURCE_DIR=<path-to-juce-8.0.15> \
  -DEHL_JUCE_DESIGN_MODULE_SOURCE_DIR=<path-to-juce-ehl-design-module> \
  -DEHL_COPY_PLUGIN_AFTER_BUILD=OFF
```

If `EHL_JUCE_DESIGN_MODULE_SOURCE_DIR` is not set, CMake uses the pinned `modules/juce-ehl-design-module` submodule and then a sibling checkout as a development fallback. Initialize submodules after cloning:

```sh
git submodule update --init --recursive
```

## Artifacts

Staged artifacts are written to:

```text
artifacts/plugin-release/macos-arm64/
artifacts/plugin-release/windows-x64/
artifacts/plugin-release/linux-x64/
```

The `ehl_stage_products` target writes `ARTIFACTS.txt` and stages:

- `standalone/spectrumming_standalone_plugin.app` or `.exe`
- `vst3/spectrumming_vst3_plugin.vst3`
- `au/spectrumming_au_plugin.component` on Apple
- `bridge/Spectrumming Bridge.app` on Apple or `bridge/Spectrumming Bridge.exe` on Windows

The AU intentionally does not declare `sandboxSafe`: the live-source path uses a companion process and local shared-frame transport. Its plist suppresses JUCE's broad default `resourceUsage` exceptions.

`OPEN BRIDGE` resolves `SPECTRUMMING_BRIDGE_PATH` first, then a bridge beside the staged VST3/AU/Standalone products, then the current working tree's staged artifacts, and finally `~/Applications` or the platform-wide application directory.

## Verification

```sh
cmake --build --preset plugin-release --parallel
ctest --preset plugin-release --output-on-failure
```

The suite covers deterministic/no-allocation DSP, scan modes, eight-voice behavior, concurrent frame ownership, embedded-image state recovery, sample-offset MIDI, 640×480 monochrome editor rendering, VST3 host loading, frame ABI validation, shared-file IPC, and artifact metadata.
