#include "ImageNormalizer.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>

namespace spectrumming::plugin
{
namespace
{
int scaledDimension(const int value, const double scale)
{
    return std::max(1, static_cast<int>(std::lround(static_cast<double>(value) * scale)));
}

std::uint8_t lumaFor(const juce::Colour colour) noexcept
{
    const auto alpha = colour.getFloatAlpha();
    const auto luma = 0.2126f * colour.getFloatRed()
                    + 0.7152f * colour.getFloatGreen()
                    + 0.0722f * colour.getFloatBlue();
    return static_cast<std::uint8_t>(std::lround(255.0f * juce::jlimit(0.0f, 1.0f, alpha * luma)));
}

juce::String contentHash(const juce::MemoryBlock& data)
{
    constexpr std::uint64_t offset = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    auto hash = offset;
    const auto* bytes = static_cast<const std::uint8_t*>(data.getData());
    for(std::size_t index = 0; index < data.getSize(); ++index)
    {
        hash ^= bytes[index];
        hash *= prime;
    }
    return juce::String::toHexString(static_cast<juce::int64>(hash)).paddedLeft('0', 16);
}
} // namespace

bool ImageNormalizer::load(const juce::File& file, LoadedImage& result, juce::String& error)
{
    if(! file.existsAsFile())
    {
        error = "IMAGE FILE NOT FOUND";
        return false;
    }

    auto stream = file.createInputStream();
    if(stream == nullptr)
    {
        error = "IMAGE FILE CANNOT OPEN";
        return false;
    }

    const auto image = juce::ImageFileFormat::loadFrom(*stream);
    if(! image.isValid())
    {
        error = "UNSUPPORTED IMAGE";
        return false;
    }

    LoadedImage loaded;
    if(! convert(image, loaded.frame, loaded.preview))
    {
        error = "IMAGE NORMALIZATION FAILED";
        return false;
    }

    loaded.displayName = file.getFileName();
    loaded.originalPath = file.getFullPathName();
    juce::MemoryBlock fileData;
    if(file.loadFileAsData(fileData))
        loaded.contentHash = contentHash(fileData);
    result = std::move(loaded);
    error.clear();
    return true;
}

bool ImageNormalizer::convert(const juce::Image& source, EmbeddedFrame& frame, juce::Image& preview)
{
    if(! source.isValid() || source.getWidth() <= 0 || source.getHeight() <= 0)
        return false;

    const auto widthScale = static_cast<double>(FrameStateCodec::maximumWidth) / source.getWidth();
    const auto heightScale = static_cast<double>(FrameStateCodec::maximumHeight) / source.getHeight();
    const auto scale = std::min({ 1.0, widthScale, heightScale });
    const auto width = scaledDimension(source.getWidth(), scale);
    const auto height = scaledDimension(source.getHeight(), scale);
    const auto normalized = source.rescaled(width, height, juce::Graphics::highResamplingQuality);

    EmbeddedFrame converted;
    converted.width = width;
    converted.height = height;
    converted.luma.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));

    juce::Image display(juce::Image::PixelFormat::RGB, width, height, true);
    for(int y = 0; y < height; ++y)
    {
        for(int x = 0; x < width; ++x)
        {
            const auto luma = lumaFor(normalized.getPixelAt(x, y));
            converted.luma[static_cast<std::size_t>(y * width + x)] = luma;
            display.setPixelAt(x, y, juce::Colour::fromRGB(luma, luma, luma));
        }
    }

    frame = std::move(converted);
    preview = std::move(display);
    return true;
}
} // namespace spectrumming::plugin
