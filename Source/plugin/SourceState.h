#pragma once

#include <juce_core/juce_core.h>

namespace spectrumming::plugin
{
enum class SourceKind
{
    image = 0,
    liveBridge = 1
};

struct SourceState final
{
    SourceKind kind = SourceKind::image;
    juce::String displayName;
    juce::String originalPath;
    juce::String contentHash;
    juce::String adapterName;
    juce::String streamId;
    bool frozen = false;
    bool stale = false;
    bool mirror = false;
    float roiX = 0.0f;
    float roiY = 0.0f;
    float roiWidth = 1.0f;
    float roiHeight = 1.0f;
    int qualityBands = 128;
};
} // namespace spectrumming::plugin
