#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <cmath>
#include <iostream>

namespace
{
int failure(const int code, const char* message)
{
    std::cerr << "[FAIL] " << message << '\n';
    return code;
}
} // namespace

int main(int argc, char** argv)
{
    if(argc != 3)
        return failure(2, "usage: SpectrummingPluginLoadTest <vst3-bundle> <expected-name>");

    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    juce::AudioPluginFormatManager manager;
    juce::addHeadlessDefaultFormatsToManager(manager);

    juce::AudioPluginFormat* vst3 = nullptr;
    for(auto* format : manager.getFormats())
        if(format != nullptr && format->getName().containsIgnoreCase("VST3"))
            vst3 = format;
    if(vst3 == nullptr)
        return failure(3, "VST3 host format is unavailable");

    juce::OwnedArray<juce::PluginDescription> descriptions;
    vst3->findAllTypesForFile(descriptions, juce::String(argv[1]));
    if(descriptions.size() != 1)
        return failure(4, "expected exactly one VST3 class");

    const auto* description = descriptions.getFirst();
    if(description == nullptr || (description->name != argv[2] && description->descriptiveName != argv[2]))
        return failure(5, "unexpected hosted name");
    if(description->manufacturerName != "EsionHsrahLatigid" || ! description->isInstrument)
        return failure(6, "hosted identity should describe an EHL instrument");

    juce::String error;
    auto instance = manager.createPluginInstance(*description, 48000.0, 256, error);
    if(instance == nullptr)
    {
        std::cerr << "VST3 instantiation failed: " << error << '\n';
        return 7;
    }
    if(! instance->acceptsMidi() || instance->getParameters().size() < 20)
    {
        std::cerr << "hosted contract: acceptsMidi=" << instance->acceptsMidi()
                  << " parameters=" << instance->getParameters().size() << '\n';
        return failure(8, "hosted MIDI or parameter contract is incomplete");
    }

    juce::MemoryBlock state;
    instance->getStateInformation(state);
    if(state.isEmpty())
        return failure(9, "hosted state should be serializable");
    instance->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    instance->prepareToPlay(48000.0, 256);

    juce::AudioBuffer<float> audio(2, 256);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    instance->processBlock(audio, midi);
    for(int channel = 0; channel < audio.getNumChannels(); ++channel)
        for(int sample = 0; sample < audio.getNumSamples(); ++sample)
            if(! std::isfinite(audio.getSample(channel, sample)))
                return failure(10, "hosted render produced a non-finite sample");

    std::cout << "loaded=" << description->name << " parameters="
              << instance->getParameters().size() << '\n';
    return 0;
}
