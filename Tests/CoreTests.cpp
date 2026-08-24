#include "../Source/core/SpectrummingCore.h"
#include "../Source/plugin/LatestFrameExchange.h"
#include "../Source/plugin/LiveFrameLiveness.h"

#include <atomic>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <thread>
#include <vector>

namespace {

std::size_t gAllocationCount = 0;
bool gCountAllocations = false;

} // namespace

void* operator new(std::size_t size)
{
    if (gCountAllocations)
        ++gAllocationCount;
    if (void* ptr = std::malloc(size))
        return ptr;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    if (gCountAllocations)
        ++gAllocationCount;
    if (void* ptr = std::malloc(size))
        return ptr;
    throw std::bad_alloc();
}

void operator delete(void* ptr) noexcept
{
    std::free(ptr);
}

void operator delete[](void* ptr) noexcept
{
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept
{
    std::free(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept
{
    std::free(ptr);
}

using namespace spectrumming::core;

namespace {

bool nearlyEqual(float a, float b, float tolerance = 0.0001f)
{
    return std::fabs(a - b) <= tolerance;
}

void testFrameSizeAndInterpolation()
{
    auto frame = std::make_unique<LumaFrame>();
    const std::uint8_t pixels[] = {
        0, 255,
        255, 0
    };

    assert(frame->setPixels(2, 2, pixels));
    assert(frame->getWidth() == 2);
    assert(frame->getHeight() == 2);
    assert(frame->getPixel(-100, 0) == 0);
    assert(frame->getPixel(100, 0) == 255);
    assert(nearlyEqual(frame->sample(0.0f, 0.0f), 0.0f));
    assert(nearlyEqual(frame->sample(1.0f, 0.0f), 1.0f));
    assert(nearlyEqual(frame->sample(0.5f, 0.5f), 0.5f, 0.002f));
    assert(!frame->setSize(0, 1));
    assert(!frame->setSize(LumaFrame::MaxWidth + 1, 1));
}

void testScanModes()
{
    ScanPhase scan;
    scan.reset(0.25f);
    assert(nearlyEqual(scan.getPosition(), 0.25f));

    scan.setMode(ScanMode::Reverse);
    assert(nearlyEqual(scan.getPosition(), 0.75f));

    scan.setMode(ScanMode::PingPong);
    assert(nearlyEqual(scan.getPosition(), 0.5f));
    scan.advance(0.50f, 1);
    assert(nearlyEqual(scan.getPhase(), 0.75f));
    assert(nearlyEqual(scan.getPosition(), 0.5f));
    scan.advance(0.50f, 1);
    assert(nearlyEqual(scan.getPhase(), 0.25f));

    scan.setCycleMode(CycleMode::OneShotStop);
    scan.setMode(ScanMode::Forward);
    scan.reset(0.75f);
    assert(!scan.advance(0.50f, 1));
    assert(nearlyEqual(scan.getPhase(), 1.0f));
    assert(scan.isStopped());

    scan.setCycleMode(CycleMode::OneShotRelease);
    scan.setMode(ScanMode::Reverse);
    scan.reset(0.25f);
    assert(scan.advance(0.50f, 1));
    assert(nearlyEqual(scan.getPhase(), 0.75f));
    assert(nearlyEqual(scan.getPosition(), 0.25f));
    assert(!scan.advance(0.50f, 1));
    assert(nearlyEqual(scan.getPhase(), 1.0f));
    assert(scan.isStopped());
}

std::unique_ptr<LumaFrame> makeGradientFrame()
{
    auto frame = std::make_unique<LumaFrame>();
    std::vector<std::uint8_t> pixels(16 * 8);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 16; ++x)
            pixels[static_cast<std::size_t>(y * 16 + x)] = static_cast<std::uint8_t>((x * 9 + y * 13) & 0xff);
    }
    assert(frame->setPixels(16, 8, pixels.data()));
    return frame;
}

void assertFiniteBounded(const float* buffer, int count)
{
    bool anyNonZero = false;
    for (int i = 0; i < count; ++i) {
        assert(std::isfinite(buffer[i]));
        assert(buffer[i] >= -1.0f);
        assert(buffer[i] <= 1.0f);
        anyNonZero = anyNonZero || std::fabs(buffer[i]) > 0.000001f;
    }
    assert(anyNonZero);
}

void testSynthPrepareBandsAndVoices()
{
    auto synth = std::make_unique<AdditiveSynth>();
    SynthConfig config;
    config.sampleRate = 48000.0;
    config.bandCount = BandCount::Bands256;
    config.scanMode = ScanMode::PingPong;
    assert(synth->prepare(config));
    assert(synth->getPreparedBandCount() == 256);
    assert(nearlyEqual(static_cast<float>(synth->getSampleRate()), 48000.0f));

    SynthConfig invalidConfig;
    invalidConfig.sampleRate = 100.0;
    invalidConfig.bandCount = BandCount::Bands64;
    assert(!synth->prepare(invalidConfig));

    const auto frame = makeGradientFrame();
    synth->setFrame(*frame);
    for (int i = 0; i < AdditiveSynth::MaxVoices + 4; ++i)
        synth->noteOn(48 + i, 0.8f);
    assert(synth->getActiveVoiceCount() == AdditiveSynth::MaxVoices);

    synth->noteOff(55);
    synth->allNotesOff();
}

void testSynthRenderDeterministicBoundedAndNoAllocations()
{
    auto a = std::make_unique<AdditiveSynth>();
    auto b = std::make_unique<AdditiveSynth>();
    SynthConfig config;
    config.sampleRate = 44100.0;
    config.bandCount = BandCount::Bands64;
    config.scanCyclesPerSecond = 3.0f;
    config.attackSeconds = 0.001f;
    config.releaseSeconds = 0.010f;

    assert(a->prepare(config));
    assert(b->prepare(config));
    const auto frame = makeGradientFrame();
    a->setFrame(*frame);
    b->setFrame(*frame);
    a->setTransposeSemitones(12);
    b->setTransposeSemitones(12);
    a->noteOn(60, 1.0f);
    b->noteOn(60, 1.0f);

    float left[256] = {};
    float right[256] = {};

    gAllocationCount = 0;
    gCountAllocations = true;
    a->render(left, 256);
    gCountAllocations = false;
    assert(gAllocationCount == 0);

    b->render(right, 256);
    for (int i = 0; i < 256; ++i)
        assert(nearlyEqual(left[i], right[i], 0.000001f));

    assertFiniteBounded(left, 256);

    a->noteOff(60);
    float releaseTail[4096] = {};
    a->render(releaseTail, 4096);
    assert(a->getActiveVoiceCount() == 0);
}

void testStereoRenderRuntimeParametersAndNoAllocations()
{
    auto synth = std::make_unique<AdditiveSynth>();
    SynthConfig config;
    config.sampleRate = 44100.0;
    config.bandCount = BandCount::Bands64;
    config.rootMidiNote = 57;
    config.startOffset = 0.25f;
    config.stereoWidth = 0.75f;
    config.outputGain = 0.5f;
    config.attackSeconds = 0.001f;
    assert(synth->prepare(config));
    const auto frame = makeGradientFrame();
    synth->setFrame(*frame);

    synth->setScanSpeed(2.0f);
    synth->setScanMode(ScanMode::Reverse);
    synth->setCycleMode(CycleMode::Loop);
    synth->setFrequencyRange(80.0f, 8000.0f);
    synth->setGamma(2.0f);
    synth->setBlackPoint(0.10f);
    synth->setInvert(true);
    synth->setFrequencySmoothing(0.20f);
    synth->setFrameSmoothing(0.25f);
    synth->setEnvelope(0.002f, 0.020f);
    synth->setOutputGain(0.75f);
    synth->setStereoWidth(1.0f);

    synth->noteOn(60, 1.0f);
    assert(nearlyEqual(synth->getVoiceScanPositionForTest(0), 0.75f));

    float left[256] = {};
    float right[256] = {};
    gAllocationCount = 0;
    gCountAllocations = true;
    synth->renderStereo(left, right, 256);
    gCountAllocations = false;
    assert(gAllocationCount == 0);

    assertFiniteBounded(left, 256);
    assertFiniteBounded(right, 256);

    bool stereoDiffers = false;
    for (int i = 0; i < 256; ++i)
        stereoDiffers = stereoDiffers || !nearlyEqual(left[i], right[i], 0.000001f);
    assert(stereoDiffers);
}

void testNoteOnRetriggersExistingVoiceFromStartOffset()
{
    auto synth = std::make_unique<AdditiveSynth>();
    SynthConfig config;
    config.sampleRate = 44100.0;
    config.bandCount = BandCount::Bands64;
    config.startOffset = 0.20f;
    config.scanCyclesPerSecond = 10.0f;
    config.attackSeconds = 0.001f;
    assert(synth->prepare(config));
    const auto frame = makeGradientFrame();
    synth->setFrame(*frame);

    synth->noteOn(60, 1.0f);
    assert(synth->getActiveVoiceCount() == 1);
    assert(nearlyEqual(synth->getVoiceScanPositionForTest(0), 0.20f));

    float buffer[256] = {};
    synth->render(buffer, 256);
    assert(!nearlyEqual(synth->getVoiceScanPositionForTest(0), 0.20f));

    synth->noteOn(60, 0.5f);
    assert(synth->getActiveVoiceCount() == 1);
    assert(nearlyEqual(synth->getVoiceScanPositionForTest(0), 0.20f));
}

void testVoiceStealingPrefersOldestReleasedVoice()
{
    auto synth = std::make_unique<AdditiveSynth>();
    SynthConfig config;
    config.bandCount = BandCount::Bands64;
    config.attackSeconds = 0.001f;
    config.releaseSeconds = 1.0f;
    assert(synth->prepare(config));
    const auto frame = makeGradientFrame();
    synth->setFrame(*frame);

    for (int i = 0; i < AdditiveSynth::MaxVoices; ++i)
        synth->noteOn(60 + i, 0.8f);
    synth->noteOff(62);
    synth->noteOff(65);
    synth->noteOn(80, 1.0f);

    assert(!synth->isNoteActiveForTest(62));
    assert(synth->isNoteActiveForTest(65));
    assert(synth->isNoteActiveForTest(60));
    assert(synth->isNoteActiveForTest(80));
}

void testOneShotVoiceReleaseAndStop()
{
    auto releaseSynth = std::make_unique<AdditiveSynth>();
    SynthConfig config;
    config.sampleRate = 16.0 * 1000.0;
    config.bandCount = BandCount::Bands64;
    config.scanCyclesPerSecond = 1000.0f;
    config.cycleMode = CycleMode::OneShotRelease;
    config.attackSeconds = 0.001f;
    config.releaseSeconds = 0.002f;
    assert(releaseSynth->prepare(config));
    const auto frame = makeGradientFrame();
    releaseSynth->setFrame(*frame);
    releaseSynth->noteOn(60, 1.0f);

    float releaseBuffer[128] = {};
    releaseSynth->render(releaseBuffer, 128);
    assert(releaseSynth->getActiveVoiceCount() == 0);

    auto stopSynth = std::make_unique<AdditiveSynth>();
    config.cycleMode = CycleMode::OneShotStop;
    assert(stopSynth->prepare(config));
    stopSynth->setFrame(*frame);
    stopSynth->noteOn(60, 1.0f);

    float stopBuffer[128] = {};
    stopSynth->render(stopBuffer, 128);
    assert(stopSynth->getActiveVoiceCount() == 0);
}

void testTranspositionChangesPitchPath()
{
    auto normal = std::make_unique<AdditiveSynth>();
    auto transposed = std::make_unique<AdditiveSynth>();
    SynthConfig config;
    config.sampleRate = 44100.0;
    config.bandCount = BandCount::Bands64;
    config.attackSeconds = 0.001f;

    assert(normal->prepare(config));
    assert(transposed->prepare(config));
    const auto frame = makeGradientFrame();
    normal->setFrame(*frame);
    transposed->setFrame(*frame);
    normal->noteOn(60, 1.0f);
    transposed->setTransposeSemitones(12);
    transposed->noteOn(60, 1.0f);

    float normalBuffer[256] = {};
    float transposedBuffer[256] = {};
    normal->render(normalBuffer, 256);
    transposed->render(transposedBuffer, 256);

    bool differs = false;
    for (int i = 0; i < 256; ++i)
        differs = differs || !nearlyEqual(normalBuffer[i], transposedBuffer[i], 0.0001f);
    assert(differs);
}

void testRootNoteChangesPitchPath()
{
    auto cRoot = std::make_unique<AdditiveSynth>();
    auto aRoot = std::make_unique<AdditiveSynth>();
    SynthConfig config;
    config.sampleRate = 44100.0;
    config.bandCount = BandCount::Bands64;
    config.attackSeconds = 0.001f;

    assert(cRoot->prepare(config));
    config.rootMidiNote = 57;
    assert(aRoot->prepare(config));
    const auto frame = makeGradientFrame();
    cRoot->setFrame(*frame);
    aRoot->setFrame(*frame);
    cRoot->noteOn(60, 1.0f);
    aRoot->noteOn(60, 1.0f);

    float cBuffer[256] = {};
    float aBuffer[256] = {};
    cRoot->render(cBuffer, 256);
    aRoot->render(aBuffer, 256);

    bool differs = false;
    for (int i = 0; i < 256; ++i)
        differs = differs || !nearlyEqual(cBuffer[i], aBuffer[i], 0.0001f);
    assert(differs);
}

void testAliasGuardStaysFinite()
{
    auto synth = std::make_unique<AdditiveSynth>();
    SynthConfig config;
    config.sampleRate = 8000.0;
    config.bandCount = BandCount::Bands256;
    config.lowFrequencyHz = 200.0f;
    config.highFrequencyHz = 20000.0f;
    assert(synth->prepare(config));
    const auto frame = makeGradientFrame();
    synth->setFrame(*frame);
    synth->setTransposeSemitones(48);
    synth->noteOn(120, 1.0f);

    float buffer[128] = {};
    synth->render(buffer, 128);
    for (float sample : buffer) {
        assert(std::isfinite(sample));
        assert(sample >= -1.0f);
        assert(sample <= 1.0f);
    }
}

void testFrameExchangeOwnershipUnderConcurrentUpdates()
{
    auto exchange = std::make_unique<spectrumming::plugin::LatestFrameExchange>();
    std::atomic<int> producersDone { 0 };
    std::atomic<bool> payloadWasConsistent { true };
    std::atomic<int> framesRead { 0 };

    const auto produceFrames = [&exchange, &producersDone](const int seed)
    {
        std::vector<std::uint8_t> pixels(64U * 32U);
        auto frame = std::make_unique<LumaFrame>();
        for(int generation = 1; generation <= 2000; ++generation)
        {
            const auto value = static_cast<std::uint8_t>((generation + seed) & 0xff);
            std::fill(pixels.begin(), pixels.end(), value);
            assert(frame->setPixels(64, 32, pixels.data()));
            exchange->publish(*frame);
        }
        producersDone.fetch_add(1, std::memory_order_release);
    };
    std::thread producerA(produceFrames, 0);
    std::thread producerB(produceFrames, 73);

    std::thread consumer([&]
    {
        auto frame = std::make_unique<LumaFrame>();
        std::uint64_t version = 0;
        while(true)
        {
            if(! exchange->consumeLatest(*frame, version))
            {
                if(producersDone.load(std::memory_order_acquire) == 2)
                    break;
                std::this_thread::yield();
                continue;
            }
            const auto expected = frame->getPixel(0, 0);
            for(int y = 0; y < frame->getHeight(); ++y)
                for(int x = 0; x < frame->getWidth(); ++x)
                    if(frame->getPixel(x, y) != expected)
                        payloadWasConsistent.store(false, std::memory_order_relaxed);
            framesRead.fetch_add(1, std::memory_order_relaxed);
        }
    });

    producerA.join();
    producerB.join();
    consumer.join();
    assert(payloadWasConsistent.load(std::memory_order_relaxed));
    assert(framesRead.load(std::memory_order_relaxed) > 0);
}

void testLiveFrameLivenessTimeoutIsDeterministic()
{
    spectrumming::plugin::LiveFrameLiveness liveness;
    liveness.beginWaiting(100.0);
    assert(! liveness.signalExpired(100.0 + spectrumming::plugin::liveFrameSignalTimeoutMs - 0.1));
    assert(liveness.signalExpired(100.0 + spectrumming::plugin::liveFrameSignalTimeoutMs));

    liveness.frameReceived(1500.0);
    assert(! liveness.signalExpired(1500.0 + spectrumming::plugin::liveFrameSignalTimeoutMs - 0.1));
    assert(liveness.signalExpired(1500.0 + spectrumming::plugin::liveFrameSignalTimeoutMs));
    assert(! liveness.signalExpired(1499.0));
}

} // namespace

int main()
{
    testFrameSizeAndInterpolation();
    testScanModes();
    testSynthPrepareBandsAndVoices();
    testSynthRenderDeterministicBoundedAndNoAllocations();
    testStereoRenderRuntimeParametersAndNoAllocations();
    testNoteOnRetriggersExistingVoiceFromStartOffset();
    testVoiceStealingPrefersOldestReleasedVoice();
    testOneShotVoiceReleaseAndStop();
    testTranspositionChangesPitchPath();
    testRootNoteChangesPitchPath();
    testAliasGuardStaysFinite();
    testFrameExchangeOwnershipUnderConcurrentUpdates();
    testLiveFrameLivenessTimeoutIsDeterministic();

    std::cout << "CoreTests passed\n";
    return 0;
}
