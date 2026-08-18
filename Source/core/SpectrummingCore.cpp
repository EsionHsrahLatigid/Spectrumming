#include "SpectrummingCore.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace spectrumming::core {
namespace {

constexpr float Pi = 3.14159265358979323846f;
constexpr float TwoPi = 2.0f * Pi;

float clamp01(float value) noexcept
{
    return std::max(0.0f, std::min(1.0f, value));
}

float wrap01(float value) noexcept
{
    value = std::fmod(value, 1.0f);
    return value < 0.0f ? value + 1.0f : value;
}

float midiToHz(float midiNote) noexcept
{
    return 440.0f * std::pow(2.0f, (midiNote - 69.0f) / 12.0f);
}

bool isSupportedBandCount(BandCount bandCount) noexcept
{
    return bandCount == BandCount::Bands64
        || bandCount == BandCount::Bands128
        || bandCount == BandCount::Bands256;
}

} // namespace

bool LumaFrame::setSize(int newWidth, int newHeight) noexcept
{
    if (newWidth < 1 || newHeight < 1 || newWidth > MaxWidth || newHeight > MaxHeight)
        return false;

    width = newWidth;
    height = newHeight;
    std::fill(data.begin(), data.end(), std::uint8_t{0});
    return true;
}

bool LumaFrame::setPixels(int newWidth, int newHeight, const std::uint8_t* pixels) noexcept
{
    if (pixels == nullptr || !setSize(newWidth, newHeight))
        return false;

    std::memcpy(data.data(), pixels, static_cast<std::size_t>(width * height));
    return true;
}

std::uint8_t LumaFrame::getPixel(int x, int y) const noexcept
{
    x = std::max(0, std::min(width - 1, x));
    y = std::max(0, std::min(height - 1, y));
    return data[static_cast<std::size_t>(y * width + x)];
}

float LumaFrame::sample(float column, float row) const noexcept
{
    if (!std::isfinite(column) || !std::isfinite(row))
        return 0.0f;

    const float x = clamp01(column) * static_cast<float>(width - 1);
    const float y = clamp01(row) * static_cast<float>(height - 1);
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(width - 1, x0 + 1);
    const int y1 = std::min(height - 1, y0 + 1);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);

    const float a = static_cast<float>(getPixel(x0, y0)) / 255.0f;
    const float b = static_cast<float>(getPixel(x1, y0)) / 255.0f;
    const float c = static_cast<float>(getPixel(x0, y1)) / 255.0f;
    const float d = static_cast<float>(getPixel(x1, y1)) / 255.0f;
    const float top = a + (b - a) * tx;
    const float bottom = c + (d - c) * tx;
    return top + (bottom - top) * ty;
}

void ScanPhase::setMode(ScanMode newMode) noexcept
{
    mode = newMode;
}

void ScanPhase::setCycleMode(CycleMode newMode) noexcept
{
    cycleMode = newMode;
}

void ScanPhase::reset(float normalizedPhase) noexcept
{
    phase = wrap01(normalizedPhase);
    stopped = false;
}

bool ScanPhase::advance(float cyclesPerSample, int samples) noexcept
{
    if (!std::isfinite(cyclesPerSample) || samples <= 0)
        return !stopped;

    if (stopped)
        return false;

    const float nextPhase = phase + std::abs(cyclesPerSample) * static_cast<float>(samples);

    if (cycleMode == CycleMode::Loop) {
        phase = wrap01(nextPhase);
        return true;
    }

    if (nextPhase >= 1.0f) {
        phase = 1.0f;
        stopped = true;
        return false;
    }

    phase = nextPhase;
    return true;
}

float ScanPhase::getPosition() const noexcept
{
    switch (mode) {
        case ScanMode::Forward:
            return phase;
        case ScanMode::Reverse:
            return 1.0f - phase;
        case ScanMode::PingPong:
            return phase < 0.5f ? phase * 2.0f : 2.0f - phase * 2.0f;
    }

    return phase;
}

bool AdditiveSynth::prepare(const SynthConfig& newConfig) noexcept
{
    if (!std::isfinite(newConfig.sampleRate)
        || newConfig.sampleRate < 8000.0
        || !isSupportedBandCount(newConfig.bandCount)
        || !std::isfinite(newConfig.lowFrequencyHz)
        || !std::isfinite(newConfig.highFrequencyHz)
        || newConfig.lowFrequencyHz <= 0.0f
        || newConfig.highFrequencyHz <= newConfig.lowFrequencyHz)
        return false;

    sampleRate = newConfig.sampleRate;
    activeBandCount = static_cast<int>(newConfig.bandCount);
    setRootMidiNote(newConfig.rootMidiNote);
    setStartOffset(newConfig.startOffset);
    setScanSpeed(newConfig.scanCyclesPerSecond);
    setScanMode(newConfig.scanMode);
    setCycleMode(newConfig.cycleMode);
    setGamma(newConfig.gamma);
    setBlackPoint(newConfig.blackPoint);
    setInvert(newConfig.invert);
    setFrequencySmoothing(newConfig.frequencySmoothing);
    setFrameSmoothing(newConfig.frameSmoothing);
    setOutputGain(newConfig.outputGain);
    setStereoWidth(newConfig.stereoWidth);
    setEnvelope(newConfig.attackSeconds, newConfig.releaseSeconds);
    configureTargetBands(newConfig.lowFrequencyHz, newConfig.highFrequencyHz);
    snapBandsToTarget();
    clearVoices();
    return true;
}

void AdditiveSynth::setFrame(const LumaFrame& newFrame) noexcept
{
    frame = newFrame;
}

void AdditiveSynth::reset() noexcept
{
    clearVoices();
}

void AdditiveSynth::noteOn(int midiNote, float velocity) noexcept
{
    if (!std::isfinite(velocity) || velocity <= 0.0f)
        return;

    int index = -1;
    for (int i = 0; i < MaxVoices; ++i) {
        const auto& existingVoice = voices[static_cast<std::size_t>(i)];
        if (existingVoice.active && existingVoice.midiNote == midiNote) {
            index = i;
            break;
        }
    }

    if (index < 0)
        index = findVoiceToSteal();

    auto& voice = voices[static_cast<std::size_t>(index)];
    voice = Voice{};
    voice.active = true;
    voice.released = false;
    voice.midiNote = midiNote;
    voice.velocity = clamp01(velocity);
    voice.envelope = 0.0f;
    voice.age = nextAge++;
    voice.scanner.setMode(scanMode);
    voice.scanner.setCycleMode(cycleMode);
    voice.scanner.reset(startOffset);

    if (nextAge == 0)
        nextAge = 1;
}

void AdditiveSynth::noteOff(int midiNote) noexcept
{
    for (auto& voice : voices) {
        if (voice.active && voice.midiNote == midiNote)
            voice.released = true;
    }
}

void AdditiveSynth::allNotesOff() noexcept
{
    for (auto& voice : voices)
        voice.released = true;
}

void AdditiveSynth::setTransposeSemitones(int semitones) noexcept
{
    transposeSemitones = std::max(-48, std::min(48, semitones));
}

void AdditiveSynth::setRootMidiNote(int midiNote) noexcept
{
    rootMidiNote = std::max(0, std::min(127, midiNote));
}

void AdditiveSynth::setStartOffset(float normalizedOffset) noexcept
{
    startOffset = std::isfinite(normalizedOffset) ? clamp01(normalizedOffset) : 0.0f;
}

void AdditiveSynth::setScanSpeed(float cyclesPerSecond) noexcept
{
    scanCyclesPerSample = std::isfinite(cyclesPerSecond)
        ? cyclesPerSecond / static_cast<float>(sampleRate)
        : 0.0f;
}

void AdditiveSynth::setScanMode(ScanMode newMode) noexcept
{
    scanMode = newMode;
    for (auto& voice : voices) {
        if (voice.active)
            voice.scanner.setMode(newMode);
    }
}

void AdditiveSynth::setCycleMode(CycleMode newMode) noexcept
{
    cycleMode = newMode;
    for (auto& voice : voices) {
        if (voice.active)
            voice.scanner.setCycleMode(newMode);
    }
}

void AdditiveSynth::setFrequencyRange(float lowHz, float highHz) noexcept
{
    if (!std::isfinite(lowHz) || !std::isfinite(highHz) || lowHz <= 0.0f || highHz <= lowHz)
        return;

    configureTargetBands(lowHz, highHz);
    if (frequencySmoothing >= 1.0f)
        snapBandsToTarget();
}

void AdditiveSynth::setGamma(float newGamma) noexcept
{
    gamma = std::isfinite(newGamma) ? std::max(0.10f, std::min(8.0f, newGamma)) : 1.0f;
}

void AdditiveSynth::setBlackPoint(float normalizedBlackPoint) noexcept
{
    blackPoint = std::isfinite(normalizedBlackPoint) ? clamp01(normalizedBlackPoint) : 0.0f;
}

void AdditiveSynth::setInvert(bool shouldInvert) noexcept
{
    invert = shouldInvert;
}

void AdditiveSynth::setFrequencySmoothing(float normalizedSmoothing) noexcept
{
    frequencySmoothing = std::isfinite(normalizedSmoothing) ? clamp01(normalizedSmoothing) : 1.0f;
}

void AdditiveSynth::setFrameSmoothing(float normalizedSmoothing) noexcept
{
    frameSmoothing = std::isfinite(normalizedSmoothing) ? clamp01(normalizedSmoothing) : 1.0f;
}

void AdditiveSynth::setEnvelope(float attackSeconds, float releaseSeconds) noexcept
{
    attackIncrement = attackSeconds > 0.0f && std::isfinite(attackSeconds)
        ? 1.0f / (attackSeconds * static_cast<float>(sampleRate))
        : 1.0f;
    releaseIncrement = releaseSeconds > 0.0f && std::isfinite(releaseSeconds)
        ? 1.0f / (releaseSeconds * static_cast<float>(sampleRate))
        : 1.0f;
}

void AdditiveSynth::setOutputGain(float gain) noexcept
{
    outputGain = std::isfinite(gain) ? std::max(0.0f, std::min(4.0f, gain)) : 1.0f;
}

void AdditiveSynth::setStereoWidth(float normalizedWidth) noexcept
{
    stereoWidth = std::isfinite(normalizedWidth) ? clamp01(normalizedWidth) : 0.0f;
}

void AdditiveSynth::render(float* output, int numSamples) noexcept
{
    renderInternal(output, output, numSamples, false);
}

void AdditiveSynth::renderStereo(float* leftOutput, float* rightOutput, int numSamples) noexcept
{
    renderInternal(leftOutput, rightOutput, numSamples, true);
}

void AdditiveSynth::renderInternal(float* leftOutput, float* rightOutput, int numSamples, bool stereo) noexcept
{
    if (leftOutput == nullptr || rightOutput == nullptr || numSamples <= 0)
        return;

    const float nyquistGuard = static_cast<float>(sampleRate) * 0.45f;
    const float voiceScale = 1.0f / static_cast<float>(MaxVoices);
    const float bandScale = activeBandCount > 0 ? 1.0f / std::sqrt(static_cast<float>(activeBandCount)) : 0.0f;
    const float frameSnap = frameSmoothing >= 1.0f ? 1.0f : frameSmoothing;

    for (int sample = 0; sample < numSamples; ++sample) {
        advanceBandSmoothing();
        float mixedLeft = 0.0f;
        float mixedRight = 0.0f;

        for (auto& voice : voices) {
            if (!voice.active)
                continue;

            if (voice.released)
                voice.envelope = std::max(0.0f, voice.envelope - releaseIncrement);
            else
                voice.envelope = std::min(1.0f, voice.envelope + attackIncrement);

            if (voice.envelope <= 0.0f && voice.released) {
                voice = Voice{};
                continue;
            }

            const float column = voice.scanner.getPosition();
            const float ratio = getTranspositionRatio(voice.midiNote);
            float voiceLeft = 0.0f;
            float voiceRight = 0.0f;

            for (int band = 0; band < activeBandCount; ++band) {
                const float frequency = bandFrequencies[static_cast<std::size_t>(band)] * ratio;
                if (frequency >= nyquistGuard)
                    continue;

                const float row = activeBandCount > 1
                    ? 1.0f - static_cast<float>(band) / static_cast<float>(activeBandCount - 1)
                    : 0.0f;
                const auto bandIndex = static_cast<std::size_t>(band);
                const float targetLevel = shapeLuma(frame.sample(column, row));
                if (frameSnap >= 1.0f)
                    voice.bandLevels[bandIndex] = targetLevel;
                else
                    voice.bandLevels[bandIndex] += (targetLevel - voice.bandLevels[bandIndex]) * frameSnap;

                const float amplitude = voice.bandLevels[bandIndex] * voice.velocity * voice.envelope;
                const float oscillator = std::sin(voice.phases[bandIndex]) * amplitude;
                const float pan = activeBandCount > 1 ? row * 2.0f - 1.0f : 0.0f;
                const float leftGain = 1.0f - stereoWidth * std::max(0.0f, pan);
                const float rightGain = 1.0f + stereoWidth * std::min(0.0f, pan);

                voiceLeft += oscillator * leftGain;
                voiceRight += oscillator * rightGain;
                voice.phases[static_cast<std::size_t>(band)] += frequency * TwoPi / static_cast<float>(sampleRate);
                if (voice.phases[static_cast<std::size_t>(band)] >= TwoPi)
                    voice.phases[static_cast<std::size_t>(band)] = std::fmod(voice.phases[static_cast<std::size_t>(band)], TwoPi);
            }

            mixedLeft += voiceLeft * bandScale * voiceScale;
            mixedRight += voiceRight * bandScale * voiceScale;

            if (!voice.scanner.advance(scanCyclesPerSample, 1)) {
                if (voice.scanner.getCycleMode() == CycleMode::OneShotStop)
                    voice = Voice{};
                else if (voice.scanner.getCycleMode() == CycleMode::OneShotRelease)
                    voice.released = true;
            }
        }

        mixedLeft *= outputGain;
        mixedRight *= outputGain;
        mixedLeft = std::isfinite(mixedLeft) ? std::max(-1.0f, std::min(1.0f, mixedLeft)) : 0.0f;
        mixedRight = std::isfinite(mixedRight) ? std::max(-1.0f, std::min(1.0f, mixedRight)) : 0.0f;

        if (stereo) {
            leftOutput[sample] = mixedLeft;
            rightOutput[sample] = mixedRight;
        } else {
            leftOutput[sample] = (mixedLeft + mixedRight) * 0.5f;
        }
    }
}

int AdditiveSynth::getActiveVoiceCount() const noexcept
{
    int count = 0;
    for (const auto& voice : voices) {
        if (voice.active)
            ++count;
    }
    return count;
}

float AdditiveSynth::getTranspositionRatio(int midiNote) const noexcept
{
    const float noteRatio = midiToHz(static_cast<float>(midiNote + transposeSemitones)) / midiToHz(static_cast<float>(rootMidiNote));
    return noteRatio;
}

int AdditiveSynth::findVoiceToSteal() const noexcept
{
    for (int i = 0; i < MaxVoices; ++i) {
        if (!voices[static_cast<std::size_t>(i)].active)
            return i;
    }

    int oldest = 0;
    std::uint32_t oldestAge = std::numeric_limits<std::uint32_t>::max();
    for (int i = 0; i < MaxVoices; ++i) {
        const auto& voice = voices[static_cast<std::size_t>(i)];
        if (voice.released && voice.age < oldestAge) {
            oldest = i;
            oldestAge = voice.age;
        }
    }
    if (oldestAge != std::numeric_limits<std::uint32_t>::max())
        return oldest;

    oldestAge = std::numeric_limits<std::uint32_t>::max();
    for (int i = 0; i < MaxVoices; ++i) {
        const auto age = voices[static_cast<std::size_t>(i)].age;
        if (age < oldestAge) {
            oldest = i;
            oldestAge = age;
        }
    }
    return oldest;
}

void AdditiveSynth::configureTargetBands(float lowHz, float highHz) noexcept
{
    const float guardedHigh = std::min(highHz, static_cast<float>(sampleRate) * 0.45f);
    const float ratio = activeBandCount > 1
        ? std::pow(guardedHigh / lowHz, 1.0f / static_cast<float>(activeBandCount - 1))
        : 1.0f;

    float frequency = lowHz;
    for (int i = 0; i < MaxBands; ++i) {
        targetBandFrequencies[static_cast<std::size_t>(i)] = i < activeBandCount ? frequency : 0.0f;
        if (i < activeBandCount)
            frequency *= ratio;
    }
}

void AdditiveSynth::snapBandsToTarget() noexcept
{
    bandFrequencies = targetBandFrequencies;
}

void AdditiveSynth::advanceBandSmoothing() noexcept
{
    if (frequencySmoothing >= 1.0f) {
        snapBandsToTarget();
        return;
    }

    for (int i = 0; i < MaxBands; ++i) {
        const auto index = static_cast<std::size_t>(i);
        bandFrequencies[index] += (targetBandFrequencies[index] - bandFrequencies[index]) * frequencySmoothing;
    }
}

float AdditiveSynth::shapeLuma(float value) const noexcept
{
    value = invert ? 1.0f - value : value;
    value = blackPoint >= 1.0f ? 0.0f : clamp01((value - blackPoint) / (1.0f - blackPoint));
    return std::pow(value, gamma);
}

void AdditiveSynth::clearVoices() noexcept
{
    for (auto& voice : voices)
        voice = Voice{};
}

float AdditiveSynth::getVoiceScanPositionForTest(int voiceIndex) const noexcept
{
    if (voiceIndex < 0 || voiceIndex >= MaxVoices)
        return 0.0f;

    const auto& voice = voices[static_cast<std::size_t>(voiceIndex)];
    return voice.active ? voice.scanner.getPosition() : 0.0f;
}

bool AdditiveSynth::isNoteActiveForTest(int midiNote) const noexcept
{
    for (const auto& voice : voices) {
        if (voice.active && voice.midiNote == midiNote)
            return true;
    }
    return false;
}

} // namespace spectrumming::core
