#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

namespace spectrumming::bridge {

constexpr std::uint32_t kFrameMagic = 0x53504246u; // "SPBF"
constexpr std::uint16_t kFrameVersion = 1u;
constexpr std::uint32_t kMaxFrameWidth = 8192u;
constexpr std::uint32_t kMaxFrameHeight = 8192u;
constexpr std::uint32_t kMaxFrameBytes = 256u * 1024u * 1024u;

enum class PixelFormat : std::uint32_t {
    unknown = 0,
    rgba8 = 1,
    bgra8 = 2,
    rgb8 = 3,
    gray8 = 4,
};

#pragma pack(push, 1)
struct FrameHeader {
    std::uint32_t magic = kFrameMagic;
    std::uint16_t version = kFrameVersion;
    std::uint16_t headerBytes = sizeof(FrameHeader);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t strideBytes = 0;
    std::uint32_t pixelFormat = static_cast<std::uint32_t>(PixelFormat::unknown);
    std::uint64_t frameId = 0;
    std::uint64_t timestampNanoseconds = 0;
    std::uint64_t streamId = 0;
};
#pragma pack(pop)

static_assert(sizeof(FrameHeader) == 48, "FrameHeader must remain a fixed 48-byte ABI.");
static_assert(std::is_trivially_copyable<FrameHeader>::value,
              "FrameHeader must remain byte-copyable.");

struct FrameView {
    FrameHeader header {};
    const std::uint8_t* pixels = nullptr;
    std::size_t pixelBytes = 0;
};

struct FrameStorage {
    FrameHeader header {};
    std::vector<std::uint8_t> pixels;
};

enum class ValidationCode {
    ok,
    badMagic,
    unsupportedVersion,
    badHeaderSize,
    unsupportedPixelFormat,
    invalidDimensions,
    invalidStride,
    frameTooLarge,
    payloadTooSmall,
    missingPayload,
    wrongStream,
};

inline const char* validationCodeName(ValidationCode code) noexcept {
    switch (code) {
        case ValidationCode::ok: return "ok";
        case ValidationCode::badMagic: return "badMagic";
        case ValidationCode::unsupportedVersion: return "unsupportedVersion";
        case ValidationCode::badHeaderSize: return "badHeaderSize";
        case ValidationCode::unsupportedPixelFormat: return "unsupportedPixelFormat";
        case ValidationCode::invalidDimensions: return "invalidDimensions";
        case ValidationCode::invalidStride: return "invalidStride";
        case ValidationCode::frameTooLarge: return "frameTooLarge";
        case ValidationCode::payloadTooSmall: return "payloadTooSmall";
        case ValidationCode::missingPayload: return "missingPayload";
        case ValidationCode::wrongStream: return "wrongStream";
    }

    return "unknown";
}

inline std::uint32_t bytesPerPixel(PixelFormat format) noexcept {
    switch (format) {
        case PixelFormat::rgba8: return 4;
        case PixelFormat::bgra8: return 4;
        case PixelFormat::rgb8: return 3;
        case PixelFormat::gray8: return 1;
        case PixelFormat::unknown: return 0;
    }

    return 0;
}

inline PixelFormat pixelFormatFromWire(std::uint32_t value) noexcept {
    switch (value) {
        case static_cast<std::uint32_t>(PixelFormat::rgba8): return PixelFormat::rgba8;
        case static_cast<std::uint32_t>(PixelFormat::bgra8): return PixelFormat::bgra8;
        case static_cast<std::uint32_t>(PixelFormat::rgb8): return PixelFormat::rgb8;
        case static_cast<std::uint32_t>(PixelFormat::gray8): return PixelFormat::gray8;
        default: return PixelFormat::unknown;
    }
}

inline bool multiplyWouldOverflow(std::size_t a, std::size_t b) noexcept {
    return b != 0 && a > std::numeric_limits<std::size_t>::max() / b;
}

inline std::size_t expectedPixelBytes(const FrameHeader& header) noexcept {
    const auto height = static_cast<std::size_t>(header.height);
    const auto stride = static_cast<std::size_t>(header.strideBytes);
    if (multiplyWouldOverflow(height, stride)) {
        return std::numeric_limits<std::size_t>::max();
    }
    return height * stride;
}

inline ValidationCode validateHeader(const FrameHeader& header,
                                     std::size_t availablePixelBytes) noexcept {
    if (header.magic != kFrameMagic) {
        return ValidationCode::badMagic;
    }
    if (header.version != kFrameVersion) {
        return ValidationCode::unsupportedVersion;
    }
    if (header.headerBytes != sizeof(FrameHeader)) {
        return ValidationCode::badHeaderSize;
    }

    const auto format = pixelFormatFromWire(header.pixelFormat);
    const auto pixelBytes = bytesPerPixel(format);
    if (pixelBytes == 0) {
        return ValidationCode::unsupportedPixelFormat;
    }

    if (header.width == 0 || header.height == 0 ||
        header.width > kMaxFrameWidth || header.height > kMaxFrameHeight) {
        return ValidationCode::invalidDimensions;
    }

    const auto width = static_cast<std::size_t>(header.width);
    if (multiplyWouldOverflow(width, pixelBytes)) {
        return ValidationCode::invalidStride;
    }

    const auto minimumStride = width * pixelBytes;
    if (header.strideBytes < minimumStride) {
        return ValidationCode::invalidStride;
    }

    const auto expectedBytes = expectedPixelBytes(header);
    if (expectedBytes == std::numeric_limits<std::size_t>::max() ||
        expectedBytes > kMaxFrameBytes) {
        return ValidationCode::frameTooLarge;
    }

    if (availablePixelBytes < expectedBytes) {
        return ValidationCode::payloadTooSmall;
    }

    return ValidationCode::ok;
}

inline ValidationCode validateFrame(const FrameView& frame) noexcept {
    if (frame.pixels == nullptr && frame.pixelBytes != 0) {
        return ValidationCode::missingPayload;
    }
    return validateHeader(frame.header, frame.pixelBytes);
}

inline FrameHeader makeFrameHeader(std::uint32_t width,
                                   std::uint32_t height,
                                   std::uint32_t strideBytes,
                                   PixelFormat pixelFormat,
                                   std::uint64_t frameId,
                                   std::uint64_t timestampNanoseconds,
                                   std::uint64_t streamId) noexcept {
    FrameHeader header {};
    header.width = width;
    header.height = height;
    header.strideBytes = strideBytes;
    header.pixelFormat = static_cast<std::uint32_t>(pixelFormat);
    header.frameId = frameId;
    header.timestampNanoseconds = timestampNanoseconds;
    header.streamId = streamId;
    return header;
}

inline bool copyValidatedFrame(const FrameView& source, FrameStorage& destination) {
    if (validateFrame(source) != ValidationCode::ok) {
        return false;
    }

    const auto bytes = expectedPixelBytes(source.header);
    destination.header = source.header;
    destination.pixels.assign(source.pixels, source.pixels + bytes);
    return true;
}

} // namespace spectrumming::bridge
