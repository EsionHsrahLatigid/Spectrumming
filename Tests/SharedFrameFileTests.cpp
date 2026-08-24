#include "bridge/SharedFrameFile.h"

#include <juce_core/juce_core.h>

#include <cstdint>
#include <iostream>
#include <vector>

namespace
{
constexpr auto childWriterArgument = "--write-frame";

bool expect(const bool condition, const char* message)
{
    if(! condition)
        std::cerr << message << '\n';
    return condition;
}

int runChildWriter(const std::uint64_t streamId)
{
    spectrumming::bridge::SharedFrameFile writer(streamId);
    std::vector<std::uint8_t> pixels(16U, 63U);
    const auto header = spectrumming::bridge::makeFrameHeader(
        4, 4, 4, spectrumming::bridge::PixelFormat::gray8, 77, 4321, streamId);
    return writer.openWriter() && writer.publish({ header, pixels.data(), pixels.size() }) ? 0 : 1;
}

bool testCrossExecutableTransport(const std::uint64_t streamId)
{
    const auto currentExecutable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    const auto childExecutable = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                     .getChildFile("SpectrummingSharedFrameWriterFixture"
                                                   + currentExecutable.getFileExtension());

    childExecutable.deleteFile();
    bool ok = expect(currentExecutable.copyFileTo(childExecutable),
                     "child writer executable copy failed");
#if ! JUCE_WINDOWS
    ok &= expect(childExecutable.setExecutePermission(true),
                 "child writer executable permission failed");
#endif

    juce::ChildProcess child;
    const juce::StringArray arguments {
        childExecutable.getFullPathName(),
        childWriterArgument,
        juce::String::toHexString(static_cast<juce::int64>(streamId))
    };
    ok &= expect(child.start(arguments), "child writer process did not start");
    if(ok)
    {
        ok &= expect(child.waitForProcessToFinish(10000), "child writer process timed out");
        ok &= expect(child.getExitCode() == 0, "child writer process failed");
    }

    spectrumming::bridge::SharedFrameFile reader(streamId);
    ok &= expect(reader.openReader(), "reader could not open frame from differently named executable");
    spectrumming::bridge::FrameStorage received;
    std::uint64_t generation = 0;
    ok &= expect(reader.readLatest(received, generation),
                 "reader could not consume frame from differently named executable");
    ok &= expect(received.header.frameId == 77, "cross-executable frame id mismatch");
    ok &= expect(received.pixels == std::vector<std::uint8_t>(16U, 63U),
                 "cross-executable payload mismatch");

    const auto backingFile = reader.backingFile();
    reader.close();
    ok &= expect(backingFile.deleteFile(), "cross-executable frame file cleanup failed");
    ok &= expect(childExecutable.deleteFile(), "child writer executable cleanup failed");
    return ok;
}
} // namespace

int main(const int argc, char* argv[])
{
    if(argc == 3 && juce::String(argv[1]) == childWriterArgument)
        return runChildWriter(static_cast<std::uint64_t>(juce::String(argv[2]).getHexValue64()));

    const auto streamId = 0x5350000000000000ULL
        ^ static_cast<std::uint64_t>(juce::Time::currentTimeMillis())
        ^ static_cast<std::uint64_t>(juce::Time::getHighResolutionTicks());

    spectrumming::bridge::SharedFrameFile writer(streamId);
    spectrumming::bridge::SharedFrameFile reader(streamId);
    bool ok = expect(writer.openWriter(), "writer did not open")
           && expect(reader.openReader(), "reader did not open");

    std::vector<std::uint8_t> pixels(16U, 127U);
    const auto header = spectrumming::bridge::makeFrameHeader(
        4, 4, 4, spectrumming::bridge::PixelFormat::gray8, 9, 1234, streamId);
    ok &= expect(writer.publish({ header, pixels.data(), pixels.size() }), "frame did not publish");

    spectrumming::bridge::FrameStorage received;
    std::uint64_t generation = 0;
    ok &= expect(reader.readLatest(received, generation), "frame did not read");
    ok &= expect(generation > 0, "generation mismatch");
    ok &= expect(received.header.frameId == 9, "frame id mismatch");
    ok &= expect(received.pixels == pixels, "payload mismatch");
    ok &= expect(! reader.readLatest(received, generation), "unchanged frame read twice");

    auto wrongStreamHeader = header;
    ++wrongStreamHeader.streamId;
    ok &= expect(writer.submitFrame({ wrongStreamHeader, pixels.data(), pixels.size() })
                     == spectrumming::bridge::ValidationCode::wrongStream,
                 "wrong stream should be reported explicitly");

    const auto backingFile = writer.backingFile();
    reader.close();
    writer.close();

    {
        juce::FileOutputStream corrupt(backingFile);
        ok &= expect(corrupt.openedOk() && corrupt.setPosition(0) && corrupt.writeByte(0),
                     "test frame header corruption failed");
        corrupt.flush();
    }
    generation = 0;
    ok &= expect(writer.openWriter(), "writer did not recover corrupt control header");
    ok &= expect(reader.openReader(), "reader did not reopen after control recovery");
    ok &= expect(writer.publish({ header, pixels.data(), pixels.size() }),
                 "frame did not publish after control recovery");
    ok &= expect(reader.readLatest(received, generation),
                 "frame did not read after control recovery");
    reader.close();
    writer.close();
    ok &= expect(backingFile.deleteFile(), "test frame file cleanup failed");
    ok &= testCrossExecutableTransport(streamId + 1U);
    return ok ? 0 : 1;
}
