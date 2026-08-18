#include "../Source/bridge/MockFrameTransport.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace spectrumming::bridge;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(expr) expect((expr), #expr, __LINE__)

std::vector<std::uint8_t> makePixels(std::size_t bytes, std::uint8_t seed = 0) {
    std::vector<std::uint8_t> pixels(bytes);
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = static_cast<std::uint8_t>((seed + i) & 0xffu);
    }
    return pixels;
}

void testHeaderLayoutAndValidation() {
    EXPECT(sizeof(FrameHeader) == 48);

    auto header = makeFrameHeader(2, 3, 8, PixelFormat::rgba8, 7, 12345, 99);
    EXPECT(validateHeader(header, 24) == ValidationCode::ok);

    auto badMagic = header;
    badMagic.magic = 0;
    EXPECT(validateHeader(badMagic, 24) == ValidationCode::badMagic);

    auto badVersion = header;
    badVersion.version = 2;
    EXPECT(validateHeader(badVersion, 24) == ValidationCode::unsupportedVersion);

    auto badHeaderSize = header;
    badHeaderSize.headerBytes = 32;
    EXPECT(validateHeader(badHeaderSize, 24) == ValidationCode::badHeaderSize);

    auto badFormat = header;
    badFormat.pixelFormat = 999;
    EXPECT(validateHeader(badFormat, 24) == ValidationCode::unsupportedPixelFormat);

    auto badDimensions = header;
    badDimensions.width = 0;
    EXPECT(validateHeader(badDimensions, 24) == ValidationCode::invalidDimensions);

    auto badStride = header;
    badStride.strideBytes = 7;
    EXPECT(validateHeader(badStride, 24) == ValidationCode::invalidStride);

    auto tooLarge = makeFrameHeader(kMaxFrameWidth, kMaxFrameHeight, kMaxFrameWidth * 4 + 1,
                                    PixelFormat::rgba8, 1, 1, 1);
    EXPECT(validateHeader(tooLarge, static_cast<std::size_t>(kMaxFrameBytes) + 1u) ==
           ValidationCode::frameTooLarge);

    EXPECT(validateHeader(header, 23) == ValidationCode::payloadTooSmall);
}

void testLatestFrameWinsWithBoundedTripleBuffer() {
    MockFrameTransport transport;
    EXPECT(transport.publishCount() == 0);

    FrameStorage latest;
    EXPECT(!transport.readLatestFrame(latest));

    for (std::uint64_t id = 1; id <= 8; ++id) {
        auto header = makeFrameHeader(2, 2, 8, PixelFormat::rgba8, id, 1000 + id, 44);
        auto pixels = makePixels(expectedPixelBytes(header), static_cast<std::uint8_t>(id));
        EXPECT(transport.submitPixels(header, pixels) == ValidationCode::ok);
    }

    EXPECT(transport.publishCount() == 8);
    EXPECT(transport.readLatestFrame(latest));
    EXPECT(latest.header.frameId == 8);
    EXPECT(latest.header.timestampNanoseconds == 1008);
    EXPECT(latest.header.streamId == 44);
    EXPECT(latest.pixels == makePixels(latest.pixels.size(), 8));
}

void testMalformedFramesDoNotReplaceLatest() {
    MockFrameTransport transport;

    auto header = makeFrameHeader(1, 1, 4, PixelFormat::rgba8, 1, 11, 22);
    auto pixels = makePixels(expectedPixelBytes(header), 4);
    EXPECT(transport.submitPixels(header, pixels) == ValidationCode::ok);

    auto malformed = header;
    malformed.frameId = 2;
    malformed.strideBytes = 3;
    EXPECT(transport.submitPixels(malformed, pixels) == ValidationCode::invalidStride);

    FrameStorage latest;
    EXPECT(transport.readLatestFrame(latest));
    EXPECT(latest.header.frameId == 1);
    EXPECT(latest.pixels == pixels);
}

} // namespace

int main() {
    testHeaderLayoutAndValidation();
    testLatestFrameWinsWithBoundedTripleBuffer();
    testMalformedFramesDoNotReplaceLatest();

    if (failures != 0) {
        std::cerr << failures << " bridge protocol test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "BridgeProtocolTests passed\n";
    return EXIT_SUCCESS;
}
