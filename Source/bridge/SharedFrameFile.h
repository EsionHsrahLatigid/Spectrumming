#pragma once

#include "BridgeInterfaces.h"

#include <juce_core/juce_core.h>

#include <cstdint>
#include <memory>

namespace spectrumming::bridge
{
class SharedFrameFile final : public IFrameSink
{
public:
    static constexpr std::uint32_t fileMagic = 0x53504653U; // SPFS
    static constexpr std::uint16_t fileVersion = 1;
    static constexpr std::size_t slotCount = 3;
    static constexpr std::size_t maximumPayloadBytes = 1024U * 512U;

    explicit SharedFrameFile(std::uint64_t streamId);
    ~SharedFrameFile() override;

    bool openWriter();
    bool openReader();
    void close();

    bool publish(const FrameView&);
    ValidationCode submitFrame(const FrameView&) override;
    bool readLatest(FrameStorage&, std::uint64_t& lastGeneration);

    bool isOpen() const noexcept { return mapping != nullptr && mapping->getData() != nullptr; }
    juce::File backingFile() const { return file; }

private:
    struct ControlHeader final
    {
        std::uint32_t magic = fileMagic;
        std::uint16_t version = fileVersion;
        std::uint16_t headerBytes = sizeof(ControlHeader);
        std::uint32_t slots = static_cast<std::uint32_t>(slotCount);
        std::uint32_t latestSlot = 0;
        std::uint64_t generation = 0;
        std::uint64_t streamId = 0;
        std::uint8_t reserved[32] {};
    };

    static constexpr std::size_t controlBytes = sizeof(ControlHeader);
    static constexpr std::size_t slotBytes = sizeof(FrameHeader) + maximumPayloadBytes;
    static constexpr std::size_t mappedBytes = controlBytes + slotCount * slotBytes;

    bool open(bool writer);
    bool initializeFile();
    bool validControl(const ControlHeader&) const noexcept;
    std::uint8_t* slotAddress(std::size_t) const noexcept;

    const std::uint64_t streamId;
    const juce::File file;
    juce::InterProcessLock lock;
    std::unique_ptr<juce::MemoryMappedFile> mapping;
};
} // namespace spectrumming::bridge
