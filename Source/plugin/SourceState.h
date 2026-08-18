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
};
} // namespace spectrumming::plugin
