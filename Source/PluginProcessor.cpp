#include "PluginProcessor.h"

#include "ParameterIDs.h"
#include "PluginEditor.h"
#include "plugin/BridgeLocator.h"
#include "plugin/ImageNormalizer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace
{
constexpr auto stateType = "SpectrummingState";
constexpr auto stateVersionProperty = "state_version";
constexpr std::array<float, 9> syncBeats { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f };

float loadFloat(const juce::AudioProcessorValueTreeState& state, const char* id)
{
    if(const auto* value = state.getRawParameterValue(id))
        return value->load(std::memory_order_relaxed);
    return 0.0f;
}

int loadInt(const juce::AudioProcessorValueTreeState& state, const char* id)
{
    return static_cast<int>(std::lround(loadFloat(state, id)));
}

float smoothingCoefficient(const float milliseconds, const double sampleRate) noexcept
{
    if(! std::isfinite(milliseconds) || milliseconds <= 0.0f || sampleRate <= 0.0)
        return 1.0f;
    const auto samples = milliseconds * 0.001 * sampleRate;
    return static_cast<float>(1.0 - std::exp(-1.0 / samples));
}

spectrumming::core::ScanMode scanModeFor(const int choice) noexcept
{
    if(choice == 1)
        return spectrumming::core::ScanMode::Reverse;
    if(choice == 2)
        return spectrumming::core::ScanMode::PingPong;
    return spectrumming::core::ScanMode::Forward;
}

spectrumming::core::CycleMode cycleModeFor(const int choice) noexcept
{
    return choice == 0 ? spectrumming::core::CycleMode::Loop
                       : spectrumming::core::CycleMode::OneShotRelease;
}
} // namespace

SpectrummingAudioProcessor::SpectrummingAudioProcessor()
    : AudioProcessor(createBusesProperties()),
      parameters(*this, nullptr, stateType, createParameterLayout())
{
    startTimerHz(30);
}

SpectrummingAudioProcessor::~SpectrummingAudioProcessor()
{
    stopTimer();
}

SpectrummingAudioProcessor::BusesProperties SpectrummingAudioProcessor::createBusesProperties()
{
    return BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true);
}

void SpectrummingAudioProcessor::prepareToPlay(const double sampleRate, const int)
{
    preparedSampleRate = std::max(8000.0, sampleRate);
    spectrumming::core::SynthConfig config;
    config.sampleRate = preparedSampleRate;
    config.bandCount = spectrumming::core::BandCount::Bands128;
    config.lowFrequencyHz = 40.0f;
    config.highFrequencyHz = 18000.0f;
    config.outputGain = juce::Decibels::decibelsToGain(-6.0f);
    synth.prepare(config);
    consumedFrameVersion = 0;
    if(frameExchange.consumeLatest(audioFrame, consumedFrameVersion))
        synth.setFrame(audioFrame);
    autoVoiceActive = false;
    lastHostPlaying = false;
    lastHostPpq = 0.0;
}

void SpectrummingAudioProcessor::releaseResources()
{
    synth.reset();
}

void SpectrummingAudioProcessor::reset()
{
    synth.reset();
    autoVoiceActive = false;
    uiOutputPeak.store(0.0f, std::memory_order_relaxed);
}

bool SpectrummingAudioProcessor::isBusesLayoutSupported(const BusesLayout& layout) const
{
    const auto output = layout.getMainOutputChannelSet();
    return layout.getMainInputChannelSet().isDisabled()
        && (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo());
}

void SpectrummingAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    if(frameExchange.consumeLatest(audioFrame, consumedFrameVersion))
        synth.setFrame(audioFrame);

    const auto transport = currentTransport();
    applyParameters(transport);
    updateAutoTrigger(transport);

    int cursor = 0;
    for(const auto metadata : midiMessages)
    {
        const auto eventPosition = juce::jlimit(cursor, buffer.getNumSamples(), metadata.samplePosition);
        renderRange(buffer, cursor, eventPosition);
        handleMidiMessage(metadata.getMessage());
        cursor = eventPosition;
    }
    renderRange(buffer, cursor, buffer.getNumSamples());

    // One-shot voices finish inside the synth, so re-arm AUTO after the
    // generated voice has fully stopped or completed its release.
    if(autoVoiceActive && synth.getActiveVoiceCount() == 0)
        autoVoiceActive = false;

    if(loadInt(parameters, spectrumming::parameters::mute) != 0)
        buffer.clear();

    float peak = 0.0f;
    for(int channel = 0; channel < buffer.getNumChannels(); ++channel)
        peak = std::max(peak, buffer.getMagnitude(channel, 0, buffer.getNumSamples()));
    uiOutputPeak.store(peak, std::memory_order_relaxed);
    uiActiveVoices.store(synth.getActiveVoiceCount(), std::memory_order_relaxed);
    uiScanPosition.store(synth.getVoiceScanPositionForTest(0), std::memory_order_relaxed);
}

void SpectrummingAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer,
                                                       juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    buffer.clear();
    uiOutputPeak.store(0.0f, std::memory_order_relaxed);
}

SpectrummingAudioProcessor::TransportSnapshot SpectrummingAudioProcessor::currentTransport() const
{
    TransportSnapshot snapshot;
    if(const auto* playHead = getPlayHead())
    {
        if(const auto position = playHead->getPosition())
        {
            if(const auto bpm = position->getBpm())
                snapshot.bpm = juce::jlimit(20.0, 400.0, *bpm);
            if(const auto ppq = position->getPpqPosition())
            {
                snapshot.ppq = *ppq;
                snapshot.validPpq = std::isfinite(*ppq);
            }
            snapshot.playing = position->getIsPlaying();
        }
    }
    return snapshot;
}

void SpectrummingAudioProcessor::applyParameters(const TransportSnapshot& transport)
{
    const auto clock = loadInt(parameters, spectrumming::parameters::clockMode);
    const auto freeSeconds = juce::jlimit(0.25f, 60.0f,
        loadFloat(parameters, spectrumming::parameters::freeDuration));
    const auto syncIndex = juce::jlimit(0, static_cast<int>(syncBeats.size()) - 1,
        loadInt(parameters, spectrumming::parameters::syncLength));
    const auto beats = syncBeats[static_cast<std::size_t>(syncIndex)];
    const auto cyclesPerSecond = clock == 0 ? 1.0f / freeSeconds
        : static_cast<float>((transport.bpm / 60.0) / beats);

    synth.setScanSpeed(cyclesPerSecond);
    synth.setScanMode(scanModeFor(loadInt(parameters, spectrumming::parameters::direction)));
    synth.setCycleMode(cycleModeFor(loadInt(parameters, spectrumming::parameters::cycleMode)));
    synth.setStartOffset(loadFloat(parameters, spectrumming::parameters::startOffset));

    auto low = loadFloat(parameters, spectrumming::parameters::lowFrequency);
    auto high = loadFloat(parameters, spectrumming::parameters::highFrequency);
    if(high <= low)
        high = std::min(20000.0f, low * std::pow(2.0f, 1.0f / 12.0f));
    synth.setFrequencyRange(low, high);
    synth.setGamma(loadFloat(parameters, spectrumming::parameters::gamma));
    synth.setBlackPoint(loadFloat(parameters, spectrumming::parameters::blackPoint));
    synth.setInvert(loadInt(parameters, spectrumming::parameters::invert) != 0);

    const auto smoothBands = loadFloat(parameters, spectrumming::parameters::frequencySmooth);
    synth.setFrequencySmoothing(1.0f / (1.0f + smoothBands * 4.0f));
    synth.setFrameSmoothing(smoothingCoefficient(
        loadFloat(parameters, spectrumming::parameters::frameSmooth), preparedSampleRate));
    synth.setRootMidiNote(loadInt(parameters, spectrumming::parameters::rootNote));
    synth.setEnvelope(loadFloat(parameters, spectrumming::parameters::attack) * 0.001f,
                      loadFloat(parameters, spectrumming::parameters::release) * 0.001f);
    synth.setStereoWidth(loadFloat(parameters, spectrumming::parameters::stereoWidth));
    synth.setOutputGain(juce::Decibels::decibelsToGain(
        loadFloat(parameters, spectrumming::parameters::outputGain)));
}

void SpectrummingAudioProcessor::updateAutoTrigger(const TransportSnapshot& transport)
{
    const auto autoMode = loadInt(parameters, spectrumming::parameters::triggerMode) == 0;
    const auto hostClock = loadInt(parameters, spectrumming::parameters::clockMode) != 0;
    const auto shouldRun = autoMode && (! hostClock || transport.playing);
    const auto root = loadInt(parameters, spectrumming::parameters::rootNote);

    bool discontinuity = false;
    if(hostClock && transport.validPpq && lastHostPlaying && transport.playing)
    {
        const auto delta = transport.ppq - lastHostPpq;
        discontinuity = delta < -0.001 || delta > 1.0;
    }

    if(autoVoiceActive && (! shouldRun || root != autoVoiceNote || discontinuity))
    {
        synth.noteOff(autoVoiceNote);
        autoVoiceActive = false;
    }

    if(shouldRun && ! autoVoiceActive)
    {
        if(hostClock && transport.validPpq)
        {
            const auto syncIndex = juce::jlimit(0, static_cast<int>(syncBeats.size()) - 1,
                loadInt(parameters, spectrumming::parameters::syncLength));
            const auto phase = std::fmod(transport.ppq / syncBeats[static_cast<std::size_t>(syncIndex)], 1.0);
            synth.setStartOffset(static_cast<float>(phase < 0.0 ? phase + 1.0 : phase));
        }
        autoVoiceNote = root;
        synth.noteOn(autoVoiceNote, 1.0f);
        autoVoiceActive = true;
    }

    lastHostPlaying = transport.playing;
    if(transport.validPpq)
        lastHostPpq = transport.ppq;
}

void SpectrummingAudioProcessor::handleMidiMessage(const juce::MidiMessage& message)
{
    if(loadInt(parameters, spectrumming::parameters::triggerMode) == 0)
        return;

    if(message.isNoteOn())
        synth.noteOn(message.getNoteNumber(), message.getFloatVelocity());
    else if(message.isNoteOff())
        synth.noteOff(message.getNoteNumber());
    else if(message.isAllNotesOff() || message.isAllSoundOff())
        synth.allNotesOff();
}

void SpectrummingAudioProcessor::renderRange(juce::AudioBuffer<float>& buffer,
                                              const int begin, const int end)
{
    const auto samples = end - begin;
    if(samples <= 0 || buffer.getNumChannels() <= 0)
        return;

    auto* left = buffer.getWritePointer(0, begin);
    if(buffer.getNumChannels() == 1)
        synth.render(left, samples);
    else
        synth.renderStereo(left, buffer.getWritePointer(1, begin), samples);

    for(int channel = 2; channel < buffer.getNumChannels(); ++channel)
        buffer.clear(channel, begin, samples);
}

juce::AudioProcessorEditor* SpectrummingAudioProcessor::createEditor()
{
    return new SpectrummingAudioProcessorEditor(*this);
}

void SpectrummingAudioProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    auto tree = parameters.copyState();
    tree.setProperty(stateVersionProperty, currentStateSchemaVersion, nullptr);
    {
        const juce::ScopedLock lock(sourceLock);
        tree.addChild(spectrumming::plugin::FrameStateCodec::encode(sourceState, embeddedFrame), -1, nullptr);
    }

    if(const auto xml = tree.createXml())
        copyXmlToBinary(*xml, destination);
}

void SpectrummingAudioProcessor::setStateInformation(const void* data, const int bytes)
{
    const auto xml = getXmlFromBinary(data, bytes);
    if(xml == nullptr || ! xml->hasTagName(parameters.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml(*xml);
    const auto schema = static_cast<int>(tree.getProperty(stateVersionProperty, 0));
    if(! tree.isValid() || schema < 1 || schema > currentStateSchemaVersion)
        return;

    const auto sourceTree = tree.getChildWithName("SOURCE");
    spectrumming::plugin::SourceState restoredSource;
    spectrumming::plugin::EmbeddedFrame restoredFrame;
    if(sourceTree.isValid()
       && ! spectrumming::plugin::FrameStateCodec::decode(sourceTree, restoredSource, restoredFrame))
        return;

    if(sourceTree.isValid())
        tree.removeChild(sourceTree, nullptr);
    parameters.replaceState(tree);

    if(restoredFrame.valid())
    {
        juce::Image restoredPreview(juce::Image::PixelFormat::RGB,
                                    restoredFrame.width, restoredFrame.height, true);
        for(int y = 0; y < restoredFrame.height; ++y)
            for(int x = 0; x < restoredFrame.width; ++x)
            {
                const auto luma = restoredFrame.luma[static_cast<std::size_t>(y * restoredFrame.width + x)];
                restoredPreview.setPixelAt(x, y, juce::Colour::fromRGB(luma, luma, luma));
            }
        publishFrame(restoredFrame, restoredPreview);
    }

    {
        const juce::ScopedLock lock(sourceLock);
        sourceState = restoredSource;
        if(sourceState.kind == spectrumming::plugin::SourceKind::image && restoredFrame.valid())
        {
            stillImageState = restoredSource;
            stillImageFrame = restoredFrame;
            stillImagePreview = previewImage.createCopy();
        }
        if(sourceState.kind == spectrumming::plugin::SourceKind::liveBridge)
        {
            sourceState.stale = true;
            sourceStatus = "STALE FRAME / WAITING FOR BRIDGE";
        }
    }
    reset();
}

juce::AudioProcessorValueTreeState::ParameterLayout SpectrummingAudioProcessor::createParameterLayout()
{
    using namespace spectrumming::parameters;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> layout;
    layout.push_back(std::make_unique<juce::AudioParameterChoice>(triggerMode, "Trigger", juce::StringArray { "auto", "midi" }, 0));
    layout.push_back(std::make_unique<juce::AudioParameterChoice>(clockMode, "Clock", juce::StringArray { "free", "host" }, 1));
    layout.push_back(std::make_unique<juce::AudioParameterFloat>(freeDuration, "Free Duration", juce::NormalisableRange<float>(0.25f, 60.0f, 0.01f, 0.35f), 8.0f));
    layout.push_back(std::make_unique<juce::AudioParameterChoice>(syncLength, "Sync Length", juce::StringArray { "1/16 bar", "1/8 bar", "1/4 bar", "1/2 bar", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars" }, 4));
    layout.push_back(std::make_unique<juce::AudioParameterChoice>(direction, "Direction", juce::StringArray { "forward", "reverse", "ping-pong" }, 0));
    layout.push_back(std::make_unique<juce::AudioParameterChoice>(cycleMode, "Cycle", juce::StringArray { "loop", "one-shot" }, 0));
    layout.push_back(std::make_unique<juce::AudioParameterFloat>(startOffset, "Start Offset", 0.0f, 1.0f, 0.0f));

    layout.push_back(std::make_unique<juce::AudioParameterFloat>(lowFrequency, "Low Frequency", juce::NormalisableRange<float>(20.0f, 2000.0f, 0.01f, 0.35f), 40.0f));
    layout.push_back(std::make_unique<juce::AudioParameterFloat>(highFrequency, "High Frequency", juce::NormalisableRange<float>(200.0f, 20000.0f, 0.01f, 0.35f), 18000.0f));
    layout.push_back(std::make_unique<juce::AudioParameterFloat>(gamma, "Gamma", juce::NormalisableRange<float>(0.25f, 4.0f, 0.01f), 1.0f));
    layout.push_back(std::make_unique<juce::AudioParameterFloat>(blackPoint, "Black Point", 0.0f, 0.95f, 0.0f));
    layout.push_back(std::make_unique<juce::AudioParameterBool>(invert, "Invert", false));
    layout.push_back(std::make_unique<juce::AudioParameterFloat>(frequencySmooth, "Frequency Smooth", 0.0f, 24.0f, 0.0f));
    layout.push_back(std::make_unique<juce::AudioParameterFloat>(frameSmooth, "Frame Smooth", 0.0f, 500.0f, 40.0f));

    layout.push_back(std::make_unique<juce::AudioParameterInt>(rootNote, "Root Note", 0, 127, 60));
    layout.push_back(std::make_unique<juce::AudioParameterFloat>(attack, "Attack", juce::NormalisableRange<float>(0.0f, 2000.0f, 0.01f, 0.35f), 5.0f));
    layout.push_back(std::make_unique<juce::AudioParameterFloat>(release, "Release", juce::NormalisableRange<float>(1.0f, 5000.0f, 0.01f, 0.35f), 150.0f));
    layout.push_back(std::make_unique<juce::AudioParameterFloat>(stereoWidth, "Stereo Width", 0.0f, 1.0f, 0.0f));
    layout.push_back(std::make_unique<juce::AudioParameterFloat>(outputGain, "Output Gain", -60.0f, 12.0f, -6.0f));
    layout.push_back(std::make_unique<juce::AudioParameterBool>(mute, "Mute", false));
    return { layout.begin(), layout.end() };
}

bool SpectrummingAudioProcessor::loadImageFile(const juce::File& file, juce::String& error)
{
    spectrumming::plugin::LoadedImage loaded;
    if(! spectrumming::plugin::ImageNormalizer::load(file, loaded, error))
    {
        setStatus(error);
        return false;
    }

    publishFrame(loaded.frame, loaded.preview);
    {
        const juce::ScopedLock lock(sourceLock);
        sourceState = {};
        sourceState.kind = spectrumming::plugin::SourceKind::image;
        sourceState.displayName = loaded.displayName;
        sourceState.originalPath = loaded.originalPath;
        sourceState.contentHash = loaded.contentHash;
        sourceState.stale = false;
        sourceState.frozen = false;
        stillImageState = sourceState;
        stillImageFrame = loaded.frame;
        stillImagePreview = loaded.preview.createCopy();
        sourceStatus = "IMAGE / " + loaded.displayName;
    }
    error.clear();
    return true;
}

void SpectrummingAudioProcessor::selectImageSource()
{
    spectrumming::plugin::EmbeddedFrame frame;
    juce::Image preview;
    {
        const juce::ScopedLock lock(sourceLock);
        if(! stillImageFrame.valid())
        {
            sourceStatus = "NO IMAGE / LOAD FILE";
            return;
        }
        sourceState = stillImageState;
        sourceState.kind = spectrumming::plugin::SourceKind::image;
        frame = stillImageFrame;
        preview = stillImagePreview.createCopy();
        sourceStatus = "IMAGE / " + sourceState.displayName;
    }
    publishFrame(frame, preview);
}

void SpectrummingAudioProcessor::selectCameraSource()
{
    {
        const juce::ScopedLock lock(sourceLock);
        sourceState = {};
        sourceState.kind = spectrumming::plugin::SourceKind::liveBridge;
        sourceState.adapterName = "UVC";
        sourceState.streamId = juce::String::toHexString(static_cast<juce::int64>(spectrumming::bridge::defaultStreamId));
        sourceState.stale = true;
        sourceStatus = "CAMERA / WAITING FOR BRIDGE";
    }
    if(! bridgeReader.isOpen())
        bridgeReader.openReader();
}

void SpectrummingAudioProcessor::setCameraFrozen(const bool frozen)
{
    const juce::ScopedLock lock(sourceLock);
    sourceState.frozen = frozen;
    sourceStatus = frozen ? "CAMERA / FROZEN" : "CAMERA / LIVE";
}

bool SpectrummingAudioProcessor::launchBridge()
{
    const auto target = findBridgeTarget();
    if(! target.exists())
    {
        setStatus("BRIDGE APP NOT FOUND");
        return false;
    }

    const auto launched = target.startAsProcess();
    setStatus(launched ? "BRIDGE LAUNCHED" : "BRIDGE LAUNCH FAILED");
    return launched;
}

spectrumming::plugin::SourceState SpectrummingAudioProcessor::sourceStateSnapshot() const
{
    const juce::ScopedLock lock(sourceLock);
    return sourceState;
}

juce::Image SpectrummingAudioProcessor::previewImageSnapshot() const
{
    const juce::ScopedLock lock(sourceLock);
    return previewImage.createCopy();
}

juce::String SpectrummingAudioProcessor::sourceStatusSnapshot() const
{
    const juce::ScopedLock lock(sourceLock);
    return sourceStatus;
}

void SpectrummingAudioProcessor::publishFrame(const spectrumming::plugin::EmbeddedFrame& frame,
                                               const juce::Image& preview)
{
    if(! frame.valid())
        return;

    spectrumming::core::LumaFrame coreFrame;
    if(! coreFrame.setPixels(frame.width, frame.height, frame.luma.data()))
        return;
    frameExchange.publish(coreFrame);

    const juce::ScopedLock lock(sourceLock);
    embeddedFrame = frame;
    previewImage = preview.createCopy();
}

void SpectrummingAudioProcessor::timerCallback()
{
    if(sourceStateSnapshot().kind == spectrumming::plugin::SourceKind::liveBridge)
        pollBridge();
}

void SpectrummingAudioProcessor::pollBridge()
{
    const auto current = sourceStateSnapshot();
    if(current.frozen)
        return;
    if(! bridgeReader.isOpen() && ! bridgeReader.openReader())
    {
        setStatus("NO SIGNAL / OPEN BRIDGE");
        return;
    }

    spectrumming::bridge::FrameStorage frame;
    if(! bridgeReader.readLatest(frame, bridgeGeneration))
        return;

    spectrumming::plugin::EmbeddedFrame embedded;
    embedded.width = static_cast<int>(frame.header.width);
    embedded.height = static_cast<int>(frame.header.height);
    embedded.luma = frame.pixels;
    juce::Image preview(juce::Image::PixelFormat::RGB, embedded.width, embedded.height, true);
    for(int y = 0; y < embedded.height; ++y)
        for(int x = 0; x < embedded.width; ++x)
        {
            const auto value = embedded.luma[static_cast<std::size_t>(y * embedded.width + x)];
            preview.setPixelAt(x, y, juce::Colour::fromRGB(value, value, value));
        }
    publishFrame(embedded, preview);

    const juce::ScopedLock lock(sourceLock);
    sourceState.displayName = "UVC " + juce::String(static_cast<juce::int64>(frame.header.frameId));
    sourceState.stale = false;
    sourceState.streamId = juce::String::toHexString(static_cast<juce::int64>(frame.header.streamId));
    sourceStatus = "CAMERA / LIVE / FRAME " + juce::String(static_cast<juce::int64>(frame.header.frameId));
}

void SpectrummingAudioProcessor::setStatus(const juce::String& status)
{
    const juce::ScopedLock lock(sourceLock);
    sourceStatus = status;
}

juce::File SpectrummingAudioProcessor::findBridgeTarget() const
{
    return spectrumming::plugin::BridgeLocator::resolve();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectrummingAudioProcessor();
}
