#pragma once

#include <juce_core/juce_core.h>

namespace spectrumming::plugin
{
class BridgeLocator final
{
public:
    static juce::File resolve();
    static juce::File findNear(const juce::File& origin);

private:
    static juce::File currentModuleFile();
    static juce::File installedBridge();
    static juce::File bridgeBelow(const juce::File& stageRoot);
};
} // namespace spectrumming::plugin
