#pragma once

#include "bridge/BridgeDefaults.h"
#include "bridge/SharedFrameFile.h"
#include "core/SpectrummingCore.h"
#include "plugin/FrameStateCodec.h"
#include "plugin/LatestFrameExchange.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cstdint>
#include <memory>

class SpectrummingAudioProcessor final : public juce::AudioProcessor,
                                         private juce::Timer
{
public:
    static constexpr int currentStateSchemaVersion = 1;

    SpectrummingAudioProcessor();
    ~SpectrummingAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState& parameterState() noexcept { return parameters; }

    bool loadImageFile(const juce::File&, juce::String& error);
    void selectImageSource();
    void selectCameraSource();
    void setCameraFrozen(bool);
    bool launchBridge();

    spectrumming::plugin::SourceState sourceStateSnapshot() const;
    juce::Image previewImageSnapshot() const;
    juce::String sourceStatusSnapshot() const;
    float scanPosition() const noexcept { return uiScanPosition.load(std::memory_order_relaxed); }
    float outputPeak() const noexcept { return uiOutputPeak.load(std::memory_order_relaxed); }
    int activeVoiceCount() const noexcept { return uiActiveVoices.load(std::memory_order_relaxed); }

private:
    struct TransportSnapshot final
    {
        double bpm = 120.0;
        double ppq = 0.0;
        bool playing = false;
        bool validPpq = false;
    };

    static BusesProperties createBusesProperties();
    TransportSnapshot currentTransport() const;
    void applyParameters(const TransportSnapshot&);
    void updateAutoTrigger(const TransportSnapshot&);
    void handleMidiMessage(const juce::MidiMessage&);
    void renderRange(juce::AudioBuffer<float>&, int begin, int end);
    void publishFrame(const spectrumming::plugin::EmbeddedFrame&, const juce::Image& preview);
    void timerCallback() override;
    void pollBridge();
    void setStatus(const juce::String&);
    juce::File findBridgeExecutable() const;

    juce::AudioProcessorValueTreeState parameters;
    spectrumming::core::AdditiveSynth synth;
    spectrumming::core::LumaFrame audioFrame;
    spectrumming::plugin::LatestFrameExchange frameExchange;
    std::uint64_t consumedFrameVersion = 0;
    double preparedSampleRate = 44100.0;

    mutable juce::CriticalSection sourceLock;
    spectrumming::plugin::SourceState sourceState;
    spectrumming::plugin::EmbeddedFrame embeddedFrame;
    juce::Image previewImage;
    spectrumming::plugin::SourceState stillImageState;
    spectrumming::plugin::EmbeddedFrame stillImageFrame;
    juce::Image stillImagePreview;
    juce::String sourceStatus { "NO INPUT" };

    spectrumming::bridge::SharedFrameFile bridgeReader { spectrumming::bridge::defaultStreamId };
    std::uint64_t bridgeGeneration = 0;
    std::unique_ptr<juce::ChildProcess> bridgeProcess;

    std::atomic<float> uiScanPosition { 0.0f };
    std::atomic<float> uiOutputPeak { 0.0f };
    std::atomic<int> uiActiveVoices { 0 };

    bool autoVoiceActive = false;
    int autoVoiceNote = 60;
    bool lastHostPlaying = false;
    double lastHostPpq = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrummingAudioProcessor)
};
