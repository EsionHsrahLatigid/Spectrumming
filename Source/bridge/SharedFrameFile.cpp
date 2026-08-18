#include "SharedFrameFile.h"

#include <cstring>

namespace spectrumming::bridge
{
namespace
{
juce::String streamSuffix(const std::uint64_t streamId)
{
    return juce::String::toHexString(static_cast<juce::int64>(streamId));
}
} // namespace

SharedFrameFile::SharedFrameFile(const std::uint64_t id)
    : streamId(id),
      file(juce::File::getSpecialLocation(juce::File::tempDirectory)
               .getChildFile("spectrumming-bridge-v1-" + streamSuffix(id) + ".frames")),
      lock("jp.ehl.spectrumming.bridge." + streamSuffix(id))
{
}

SharedFrameFile::~SharedFrameFile() = default;

bool SharedFrameFile::openWriter()
{
    return open(true);
}

bool SharedFrameFile::openReader()
{
    return open(false);
}

void SharedFrameFile::close()
{
    mapping.reset();
}

bool SharedFrameFile::publish(const FrameView& frame)
{
    if(! isOpen() || frame.header.streamId != streamId
       || frame.header.pixelFormat != static_cast<std::uint32_t>(PixelFormat::gray8)
       || validateFrame(frame) != ValidationCode::ok
       || expectedPixelBytes(frame.header) > maximumPayloadBytes)
        return false;

    if(! lock.enter(4))
        return false;

    auto* control = static_cast<ControlHeader*>(mapping->getData());
    if(! validControl(*control))
    {
        lock.exit();
        return false;
    }

    const auto next = (static_cast<std::size_t>(control->latestSlot) + 1U) % slotCount;
    auto* destination = slotAddress(next);
    std::memcpy(destination, &frame.header, sizeof(FrameHeader));
    std::memcpy(destination + sizeof(FrameHeader), frame.pixels, expectedPixelBytes(frame.header));
    control->latestSlot = static_cast<std::uint32_t>(next);
    ++control->generation;
    lock.exit();
    return true;
}

ValidationCode SharedFrameFile::submitFrame(const FrameView& frame)
{
    const auto validation = validateFrame(frame);
    if(validation != ValidationCode::ok)
        return validation;
    if(frame.header.streamId != streamId)
        return ValidationCode::wrongStream;
    if(frame.header.pixelFormat != static_cast<std::uint32_t>(PixelFormat::gray8))
        return ValidationCode::unsupportedPixelFormat;
    if(expectedPixelBytes(frame.header) > maximumPayloadBytes)
        return ValidationCode::frameTooLarge;
    return publish(frame) ? ValidationCode::ok : ValidationCode::missingPayload;
}

bool SharedFrameFile::readLatest(FrameStorage& destination, std::uint64_t& lastGeneration)
{
    if(! isOpen() || ! lock.enter(4))
        return false;

    const auto* control = static_cast<const ControlHeader*>(mapping->getData());
    if(! validControl(*control) || control->generation == 0 || control->generation == lastGeneration)
    {
        lock.exit();
        return false;
    }

    const auto* source = slotAddress(control->latestSlot);
    FrameHeader header;
    std::memcpy(&header, source, sizeof(header));
    const auto bytes = expectedPixelBytes(header);
    if(header.streamId != streamId || header.pixelFormat != static_cast<std::uint32_t>(PixelFormat::gray8)
       || bytes > maximumPayloadBytes || validateHeader(header, bytes) != ValidationCode::ok)
    {
        lock.exit();
        return false;
    }

    destination.header = header;
    destination.pixels.assign(source + sizeof(FrameHeader), source + sizeof(FrameHeader) + bytes);
    lastGeneration = control->generation;
    lock.exit();
    return true;
}

bool SharedFrameFile::open(const bool writer)
{
    close();
    if(writer)
    {
        if(! initializeFile())
            return false;
    }
    else if(! file.existsAsFile() || file.getSize() != static_cast<juce::int64>(mappedBytes))
    {
        return false;
    }

#if JUCE_WINDOWS
    // JUCE's Windows read-only mapping omits FILE_SHARE_WRITE, so it cannot be
    // opened while the bridge keeps its read-write mapping alive. The reader
    // still treats the mapped bytes as const; read-write access is only needed
    // to preserve the live writer/reader sharing contract on Windows.
    constexpr auto readerAccess = juce::MemoryMappedFile::readWrite;
#else
    constexpr auto readerAccess = juce::MemoryMappedFile::readOnly;
#endif
    mapping = std::make_unique<juce::MemoryMappedFile>(
        file, writer ? juce::MemoryMappedFile::readWrite : readerAccess, false);
    return isOpen() && mapping->getSize() == mappedBytes;
}

bool SharedFrameFile::initializeFile()
{
    if(! lock.enter(250))
        return false;

    const auto needsResize = ! file.existsAsFile()
        || file.getSize() != static_cast<juce::int64>(mappedBytes);

    if(needsResize)
    {
        if(! file.getParentDirectory().createDirectory())
        {
            lock.exit();
            return false;
        }

        if(file.existsAsFile() && ! file.deleteFile())
        {
            lock.exit();
            return false;
        }

        juce::FileOutputStream stream(file);
        if(! stream.openedOk() || ! stream.setPosition(static_cast<juce::int64>(mappedBytes - 1U))
           || ! stream.writeByte(0))
        {
            lock.exit();
            return false;
        }
        stream.flush();
    }

    juce::MemoryMappedFile initialMapping(file, juce::MemoryMappedFile::readWrite, false);
    if(initialMapping.getData() == nullptr || initialMapping.getSize() != mappedBytes)
    {
        lock.exit();
        return false;
    }

    auto* control = static_cast<ControlHeader*>(initialMapping.getData());
    if(needsResize || ! validControl(*control))
    {
        std::memset(initialMapping.getData(), 0, mappedBytes);
        control = static_cast<ControlHeader*>(initialMapping.getData());
        *control = ControlHeader{};
        control->streamId = streamId;
    }

    lock.exit();
    return true;
}

bool SharedFrameFile::validControl(const ControlHeader& control) const noexcept
{
    return control.magic == fileMagic && control.version == fileVersion
        && control.headerBytes == sizeof(ControlHeader) && control.slots == slotCount
        && control.latestSlot < slotCount && control.streamId == streamId;
}

std::uint8_t* SharedFrameFile::slotAddress(const std::size_t index) const noexcept
{
    return static_cast<std::uint8_t*>(mapping->getData()) + controlBytes + index * slotBytes;
}
} // namespace spectrumming::bridge
