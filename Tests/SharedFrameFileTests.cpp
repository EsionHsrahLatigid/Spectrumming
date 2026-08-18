#include "bridge/SharedFrameFile.h"

#include <juce_core/juce_core.h>

#include <cstdint>
#include <iostream>
#include <vector>

namespace
{
bool expect(const bool condition, const char* message)
{
    if(! condition)
        std::cerr << message << '\n';
    return condition;
}
} // namespace

int main()
{
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

    const auto backingFile = writer.backingFile();
    reader.close();
    writer.close();
    ok &= expect(backingFile.deleteFile(), "test frame file cleanup failed");
    return ok ? 0 : 1;
}
