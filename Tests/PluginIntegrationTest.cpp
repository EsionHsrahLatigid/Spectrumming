#include "ParameterIDs.h"
#include "PluginProcessor.h"
#include "plugin/BridgeLocator.h"
#include "plugin/PreviewPlacement.h"

#include <juce_events/juce_events.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>

namespace
{
bool check(const bool condition, const char* message)
{
    if(! condition)
        std::cerr << "[FAIL] " << message << '\n';
    return condition;
}

void setParameter(SpectrummingAudioProcessor& processor, const char* id, const float value)
{
    if(auto* parameter = processor.parameterState().getParameter(id))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

juce::File makeTestImage()
{
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getNonexistentChildFile("spectrumming-integration", ".png", false);
    juce::Image image(juce::Image::RGB, 64, 32, true);
    image.clear(image.getBounds(), juce::Colours::white);
    if(auto stream = file.createOutputStream())
    {
        juce::PNGImageFormat png;
        png.writeImageToStream(image, *stream);
        stream->flush();
    }
    return file;
}

bool verifyFinite(const juce::AudioBuffer<float>& audio)
{
    for(int channel = 0; channel < audio.getNumChannels(); ++channel)
        for(int sample = 0; sample < audio.getNumSamples(); ++sample)
            if(! std::isfinite(audio.getSample(channel, sample)))
                return false;
    return true;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    auto processor = std::make_unique<SpectrummingAudioProcessor>();
    bool passed = true;

    passed &= check(processor->getName() == "Spectrumming", "product identity should be Spectrumming");
    passed &= check(processor->acceptsMidi(), "instrument should accept MIDI");
    passed &= check(! processor->isMidiEffect(), "instrument should not be a MIDI effect");
    passed &= check(processor->getParameters().size() == 20, "public parameter contract should contain 20 controls");

    const auto locatorRoot = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getNonexistentChildFile("spectrumming-locator", {}, false);
#if JUCE_MAC
    const auto fakeModule = locatorRoot.getChildFile(
        "vst3/Spectrumming.vst3/Contents/MacOS/Spectrumming");
    const auto fakeBridge = locatorRoot.getChildFile("bridge/Spectrumming Bridge.app");
    const auto fakeBridgeExecutable = fakeBridge.getChildFile(
        "Contents/MacOS/Spectrumming Bridge");
#elif JUCE_WINDOWS
    const auto fakeModule = locatorRoot.getChildFile("vst3/Spectrumming.vst3/Contents/x86_64-win/Spectrumming.vst3");
    const auto fakeBridge = locatorRoot.getChildFile("bridge/Spectrumming Bridge.exe");
    const auto fakeBridgeExecutable = fakeBridge;
#else
    const auto fakeModule = locatorRoot.getChildFile("vst3/Spectrumming.vst3");
    const auto fakeBridge = juce::File {};
    const auto fakeBridgeExecutable = juce::File {};
#endif
    fakeModule.getParentDirectory().createDirectory();
    fakeModule.replaceWithText("module");
    if(fakeBridge != juce::File {})
    {
        fakeBridgeExecutable.getParentDirectory().createDirectory();
        fakeBridgeExecutable.replaceWithText("bridge");
        passed &= check(spectrumming::plugin::BridgeLocator::findNear(fakeModule) == fakeBridge,
                        "bridge locator should resolve the launchable staged companion");
    }
    locatorRoot.deleteRecursively();

    if(juce::SystemStats::getEnvironmentVariable("SPECTRUMMING_BRIDGE_LAUNCH_SMOKE", {}) == "1")
        passed &= check(processor->launchBridge(),
                        "bridge app should launch through the platform application service");

    juce::AudioProcessor::BusesLayout stereo;
    stereo.outputBuses.add(juce::AudioChannelSet::stereo());
    passed &= check(processor->isBusesLayoutSupported(stereo), "stereo synth output should be supported");

    const auto imageFile = makeTestImage();
    juce::String error;
    passed &= check(processor->loadImageFile(imageFile, error), "test image should load");
    passed &= check(processor->previewImageSnapshot().isValid(), "loaded image should create a preview");

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
    passed &= check(editor != nullptr, "processor should create an editor");
    if(editor != nullptr)
    {
        editor->resized();
        passed &= check(editor->getWidth() == 640 && editor->getHeight() == 480,
                        "editor should use the approved 640x480 footprint");
        juce::Image capture(juce::Image::RGB, 640, 480, true);
        juce::Graphics graphics(capture);
        editor->paintEntireComponent(graphics, true);
        const juce::Rectangle<float> sourceArea { 0.0f, 0.0f, 64.0f, 32.0f };
        const juce::Rectangle<float> displayArea { 0.0f, 0.0f, 592.0f, 82.0f };
        const auto placedArea = spectrumming::ui::previewImagePlacement().appliedTo(
            sourceArea, displayArea);
        passed &= check(placedArea == displayArea,
                        "preview image placement should stretch to the full display");
        passed &= check(std::abs(placedArea.getWidth() / placedArea.getHeight()
                                    - sourceArea.getWidth() / sourceArea.getHeight())
                            > 0.0001f,
                        "preview image placement should not preserve aspect ratio");
        const auto capturePath = juce::SystemStats::getEnvironmentVariable(
            "SPECTRUMMING_EDITOR_CAPTURE", {});
        if(capturePath.isNotEmpty())
            if(auto output = juce::File(capturePath).createOutputStream())
            {
                juce::PNGImageFormat png;
                passed &= check(png.writeImageToStream(capture, *output),
                                "editor capture should be writable");
            }
        for(int y = 0; y < capture.getHeight(); y += 8)
            for(int x = 0; x < capture.getWidth(); x += 8)
            {
                const auto colour = capture.getPixelAt(x, y);
                const auto maximum = std::max({ colour.getRed(), colour.getGreen(), colour.getBlue() });
                const auto minimum = std::min({ colour.getRed(), colour.getGreen(), colour.getBlue() });
                passed &= check(static_cast<int>(maximum) - static_cast<int>(minimum) <= 8,
                                "editor should remain monochrome");
            }
    }

    processor->selectCameraSource();
    passed &= check(processor->sourceStateSnapshot().kind
                        == spectrumming::plugin::SourceKind::liveBridge,
                    "camera command should select the neutral live bridge source");
    processor->setCameraFrozen(true);
    passed &= check(processor->sourceStateSnapshot().frozen
                        && processor->sourceStatusSnapshot() == "CAMERA / FROZEN",
                    "freeze should retain an explicit camera state");
    processor->setCameraFrozen(false);
    passed &= check(processor->sourceStateSnapshot().stale
                        && processor->sourceStatusSnapshot() == "CAMERA / WAITING FOR BRIDGE",
                    "unfreeze should wait for a newly arriving live frame");
    processor->selectImageSource();
    const auto restoredPreview = processor->previewImageSnapshot();
    passed &= check(processor->sourceStateSnapshot().kind
                        == spectrumming::plugin::SourceKind::image
                        && restoredPreview.isValid(),
                    "returning to image should restore the retained still frame");
    passed &= check(restoredPreview.isValid()
                        && restoredPreview.getPixelAt(restoredPreview.getWidth() / 2,
                                                      restoredPreview.getHeight() / 2).getBrightness() > 0.99f,
                    "white test image should normalize to a bright frame");

    setParameter(*processor, spectrumming::parameters::triggerMode, 1.0f);
    setParameter(*processor, spectrumming::parameters::attack, 0.0f);
    setParameter(*processor, spectrumming::parameters::release, 10.0f);
    setParameter(*processor, spectrumming::parameters::frameSmooth, 0.0f);
    processor->prepareToPlay(48000.0, 256);

    juce::AudioBuffer<float> audio(2, 256);
    audio.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 64);
    processor->processBlock(audio, midi);
    passed &= check(verifyFinite(audio), "image-derived audio should remain finite");
    passed &= check(audio.getMagnitude(0, 0, 64) == 0.0f,
                    "sample-offset MIDI should preserve silence before note-on");
    passed &= check(processor->activeVoiceCount() > 0,
                    "sample-offset note-on should activate a synth voice");

    auto audiblePeak = audio.getMagnitude(0, 64, audio.getNumSamples() - 64);
    midi.clear();
    for(int block = 0; block < 8 && audiblePeak <= 0.001f; ++block)
    {
        audio.clear();
        processor->processBlock(audio, midi);
        audiblePeak = std::max(audiblePeak,
                               audio.getMagnitude(0, 0, audio.getNumSamples()));
    }
    if(audiblePeak <= 0.001f)
        std::cerr << "[DIAG] sustained white-frame peak=" << audiblePeak
                  << " activeVoices=" << processor->activeVoiceCount() << '\n';
    passed &= check(audiblePeak > 0.001f,
                    "white image should produce audible spectral output after note-on");

    setParameter(*processor, spectrumming::parameters::triggerMode, 0.0f);
    setParameter(*processor, spectrumming::parameters::clockMode, 0.0f);
    setParameter(*processor, spectrumming::parameters::cycleMode, 1.0f);
    setParameter(*processor, spectrumming::parameters::freeDuration, 0.25f);
    setParameter(*processor, spectrumming::parameters::release, 0.0f);
    processor->reset();

    bool observedOneShotCompletion = false;
    for(int block = 0; block < 96 && ! observedOneShotCompletion; ++block)
    {
        audio.clear();
        midi.clear();
        processor->processBlock(audio, midi);
        observedOneShotCompletion = processor->activeVoiceCount() == 0;
    }
    passed &= check(observedOneShotCompletion,
                    "AUTO one-shot should complete and expose an idle synth");

    audio.clear();
    processor->processBlock(audio, midi);
    passed &= check(processor->activeVoiceCount() > 0,
                    "AUTO one-shot should re-arm and trigger the next cycle");

    setParameter(*processor, spectrumming::parameters::gamma, 2.5f);
    juce::MemoryBlock state;
    processor->getStateInformation(state);
    passed &= check(state.getSize() > 128, "state should include parameters and compressed image frame");
    imageFile.deleteFile();

    auto restored = std::make_unique<SpectrummingAudioProcessor>();
    restored->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    passed &= check(restored->previewImageSnapshot().isValid(),
                    "embedded frame should restore without the original image file");
    passed &= check(std::abs(restored->parameterState().getRawParameterValue(
                                 spectrumming::parameters::gamma)->load() - 2.5f) < 0.01f,
                    "parameter state should round-trip");
    passed &= check(restored->sourceStateSnapshot().kind == spectrumming::plugin::SourceKind::image,
                    "source kind should round-trip");

    if(passed)
        std::cout << "Spectrumming plug-in integration checks passed\n";
    return passed ? 0 : 1;
}
