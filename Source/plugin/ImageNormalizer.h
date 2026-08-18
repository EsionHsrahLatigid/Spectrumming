#pragma once

#include "FrameStateCodec.h"

#include <juce_graphics/juce_graphics.h>

namespace spectrumming::plugin
{
struct LoadedImage final
{
    EmbeddedFrame frame;
    juce::Image preview;
    juce::String displayName;
    juce::String originalPath;
    juce::String contentHash;
};

class ImageNormalizer final
{
public:
    static bool load(const juce::File&, LoadedImage&, juce::String& error);
    static bool convert(const juce::Image&, EmbeddedFrame&, juce::Image& preview);
};
} // namespace spectrumming::plugin
