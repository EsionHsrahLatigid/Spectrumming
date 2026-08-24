#pragma once

#include <juce_graphics/juce_graphics.h>

namespace spectrumming::ui
{
inline juce::RectanglePlacement previewImagePlacement() noexcept
{
    return { juce::RectanglePlacement::stretchToFit };
}
} // namespace spectrumming::ui
