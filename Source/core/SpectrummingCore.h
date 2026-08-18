#pragma once

#include <array>
#include <cstdint>

namespace spectrumming::core {

class LumaFrame {
public:
    static constexpr int MaxWidth = 1024;
    static constexpr int MaxHeight = 512;
    static constexpr int MaxPixels = MaxWidth * MaxHeight;

    bool setSize(int newWidth, int newHeight) noexcept;
    bool setPixels(int newWidth, int newHeight, const std::uint8_t* pixels) noexcept;

    int getWidth() const noexcept { return width; }
    int getHeight() const noexcept { return height; }
    const std::uint8_t* getPixels() const noexcept { return data.data(); }

    std::uint8_t getPixel(int x, int y) const noexcept;
    float sample(float column, float row) const noexcept;

private:
    int width = 1;
    int height = 1;
    std::array<std::uint8_t, MaxPixels> data{};
};

enum class ScanMode {
    Forward,
    Reverse,
    PingPong
};

enum class CycleMode {
    Loop,
    OneShotRelease,
    OneShotStop
};

class ScanPhase {
public:
    void setMode(ScanMode newMode) noexcept;
    void setCycleMode(CycleMode newMode) noexcept;
    void reset(float normalizedPhase = 0.0f) noexcept;
    bool advance(float cyclesPerSample, int samples) noexcept;

    ScanMode getMode() const noexcept { return mode; }
    CycleMode getCycleMode() const noexcept { return cycleMode; }
    float getPhase() const noexcept { return phase; }
    float getPosition() const noexcept;
    bool isStopped() const noexcept { return stopped; }

private:
    ScanMode mode = ScanMode::Forward;
    CycleMode cycleMode = CycleMode::Loop;
    float phase = 0.0f;
    bool stopped = false;
};

enum class BandCount {
    Bands64 = 64,
    Bands128 = 128,
    Bands256 = 256
};

struct SynthConfig {
    double sampleRate = 44100.0;
    BandCount bandCount = BandCount::Bands128;
    float lowFrequencyHz = 55.0f;
    float highFrequencyHz = 12000.0f;
    float scanCyclesPerSecond = 0.25f;
    ScanMode scanMode = ScanMode::Forward;
    CycleMode cycleMode = CycleMode::Loop;
    int rootMidiNote = 60;
    float startOffset = 0.0f;
    float gamma = 1.0f;
    float blackPoint = 0.0f;
    bool invert = false;
    float frequencySmoothing = 1.0f;
    float frameSmoothing = 1.0f;
    float outputGain = 1.0f;
    float stereoWidth = 0.0f;
    float attackSeconds = 0.005f;
    float releaseSeconds = 0.080f;
};

class AdditiveSynth {
public:
    static constexpr int MaxBands = 256;
    static constexpr int MaxVoices = 8;

    bool prepare(const SynthConfig& newConfig) noexcept;
    void setFrame(const LumaFrame& newFrame) noexcept;
    void reset() noexcept;

    void noteOn(int midiNote, float velocity) noexcept;
    void noteOff(int midiNote) noexcept;
    void allNotesOff() noexcept;
    void setTransposeSemitones(int semitones) noexcept;
    void setRootMidiNote(int midiNote) noexcept;
    void setStartOffset(float normalizedOffset) noexcept;
    void setScanSpeed(float cyclesPerSecond) noexcept;
    void setScanMode(ScanMode newMode) noexcept;
    void setCycleMode(CycleMode newMode) noexcept;
    void setFrequencyRange(float lowHz, float highHz) noexcept;
    void setGamma(float newGamma) noexcept;
    void setBlackPoint(float normalizedBlackPoint) noexcept;
    void setInvert(bool shouldInvert) noexcept;
    void setFrequencySmoothing(float normalizedSmoothing) noexcept;
    void setFrameSmoothing(float normalizedSmoothing) noexcept;
    void setEnvelope(float attackSeconds, float releaseSeconds) noexcept;
    void setOutputGain(float gain) noexcept;
    void setStereoWidth(float normalizedWidth) noexcept;

    void render(float* output, int numSamples) noexcept;
    void renderStereo(float* leftOutput, float* rightOutput, int numSamples) noexcept;

    int getActiveVoiceCount() const noexcept;
    int getPreparedBandCount() const noexcept { return activeBandCount; }
    double getSampleRate() const noexcept { return sampleRate; }
    float getVoiceScanPositionForTest(int voiceIndex) const noexcept;
    bool isNoteActiveForTest(int midiNote) const noexcept;

private:
    struct Voice {
        bool active = false;
        bool released = false;
        int midiNote = 0;
        float velocity = 0.0f;
        float envelope = 0.0f;
        std::uint32_t age = 0;
        ScanPhase scanner;
        std::array<float, MaxBands> phases{};
        std::array<float, MaxBands> bandLevels{};
    };

    float getTranspositionRatio(int midiNote) const noexcept;
    int findVoiceToSteal() const noexcept;
    void configureTargetBands(float lowHz, float highHz) noexcept;
    void snapBandsToTarget() noexcept;
    void advanceBandSmoothing() noexcept;
    float shapeLuma(float value) const noexcept;
    void renderInternal(float* leftOutput, float* rightOutput, int numSamples, bool stereo) noexcept;
    void clearVoices() noexcept;

    LumaFrame frame;
    std::array<Voice, MaxVoices> voices{};
    std::array<float, MaxBands> bandFrequencies{};
    std::array<float, MaxBands> targetBandFrequencies{};

    double sampleRate = 44100.0;
    int activeBandCount = 128;
    int rootMidiNote = 60;
    int transposeSemitones = 0;
    float startOffset = 0.0f;
    float scanCyclesPerSample = 0.25f / 44100.0f;
    ScanMode scanMode = ScanMode::Forward;
    CycleMode cycleMode = CycleMode::Loop;
    float gamma = 1.0f;
    float blackPoint = 0.0f;
    bool invert = false;
    float frequencySmoothing = 1.0f;
    float frameSmoothing = 1.0f;
    float outputGain = 1.0f;
    float stereoWidth = 0.0f;
    float attackIncrement = 1.0f;
    float releaseIncrement = 1.0f;
    std::uint32_t nextAge = 1;
};

} // namespace spectrumming::core
