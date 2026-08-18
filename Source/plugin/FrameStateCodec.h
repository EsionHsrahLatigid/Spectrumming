#pragma once

#include "SourceState.h"

#include <juce_data_structures/juce_data_structures.h>

#include <cstdint>
#include <vector>

namespace spectrumming::plugin
{
struct EmbeddedFrame final
{
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> luma;

    bool valid() const noexcept;
};

class FrameStateCodec final
{
public:
    static constexpr int currentSchemaVersion = 1;
    static constexpr int maximumWidth = 1024;
    static constexpr int maximumHeight = 512;

    static juce::ValueTree encode(const SourceState&, const EmbeddedFrame&);
    static bool decode(const juce::ValueTree&, SourceState&, EmbeddedFrame&);

private:
    static bool compress(const EmbeddedFrame&, juce::MemoryBlock&);
    static bool decompress(const juce::MemoryBlock&, EmbeddedFrame&);
};
} // namespace spectrumming::plugin
