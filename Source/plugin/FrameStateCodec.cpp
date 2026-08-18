#include "FrameStateCodec.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <limits>

namespace spectrumming::plugin
{
namespace
{
constexpr auto sourceTreeType = "SOURCE";
constexpr auto frameProperty = "frame_zlib";
constexpr std::uint32_t frameMagic = 0x53504631U; // SPF1

bool validDimensions(const int width, const int height) noexcept
{
    return width > 0 && height > 0
        && width <= FrameStateCodec::maximumWidth
        && height <= FrameStateCodec::maximumHeight
        && static_cast<std::size_t>(width) <= std::numeric_limits<std::size_t>::max()
                                                 / static_cast<std::size_t>(height);
}
} // namespace

bool EmbeddedFrame::valid() const noexcept
{
    return validDimensions(width, height)
        && luma.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
}

juce::ValueTree FrameStateCodec::encode(const SourceState& state, const EmbeddedFrame& frame)
{
    juce::ValueTree tree(sourceTreeType);
    tree.setProperty("schema_version", currentSchemaVersion, nullptr);
    tree.setProperty("kind", static_cast<int>(state.kind), nullptr);
    tree.setProperty("display_name", state.displayName, nullptr);
    tree.setProperty("original_path", state.originalPath, nullptr);
    tree.setProperty("content_hash", state.contentHash, nullptr);
    tree.setProperty("adapter_name", state.adapterName, nullptr);
    tree.setProperty("stream_id", state.streamId, nullptr);
    tree.setProperty("frozen", state.frozen, nullptr);
    tree.setProperty("stale", state.stale, nullptr);

    juce::MemoryBlock compressed;
    if(compress(frame, compressed))
        tree.setProperty(frameProperty, compressed, nullptr);

    return tree;
}

bool FrameStateCodec::decode(const juce::ValueTree& tree, SourceState& state, EmbeddedFrame& frame)
{
    if(! tree.isValid() || tree.getType().toString() != sourceTreeType)
        return false;

    const auto schema = static_cast<int>(tree.getProperty("schema_version", 0));
    if(schema < 1 || schema > currentSchemaVersion)
        return false;

    const auto kind = static_cast<int>(tree.getProperty("kind", 0));
    state.kind = kind == static_cast<int>(SourceKind::liveBridge) ? SourceKind::liveBridge : SourceKind::image;
    state.displayName = tree.getProperty("display_name").toString();
    state.originalPath = tree.getProperty("original_path").toString();
    state.contentHash = tree.getProperty("content_hash").toString();
    state.adapterName = tree.getProperty("adapter_name").toString();
    state.streamId = tree.getProperty("stream_id").toString();
    state.frozen = static_cast<bool>(tree.getProperty("frozen", false));
    state.stale = static_cast<bool>(tree.getProperty("stale", false));

    frame = {};
    const auto* binary = tree.getProperty(frameProperty).getBinaryData();
    return binary == nullptr || decompress(*binary, frame);
}

bool FrameStateCodec::compress(const EmbeddedFrame& frame, juce::MemoryBlock& output)
{
    if(! frame.valid())
        return false;

    juce::MemoryOutputStream destination(output, false);
    {
        juce::GZIPCompressorOutputStream compressor(&destination, 6, false);
        if(! compressor.writeInt(static_cast<int>(frameMagic))
           || ! compressor.writeInt(frame.width)
           || ! compressor.writeInt(frame.height)
           || ! compressor.write(frame.luma.data(), frame.luma.size()))
            return false;
    }
    return output.getSize() > 0;
}

bool FrameStateCodec::decompress(const juce::MemoryBlock& input, EmbeddedFrame& frame)
{
    juce::MemoryInputStream source(input, false);
    juce::GZIPDecompressorInputStream decompressor(source);

    const auto magic = static_cast<std::uint32_t>(decompressor.readInt());
    const auto width = decompressor.readInt();
    const auto height = decompressor.readInt();
    if(magic != frameMagic || ! validDimensions(width, height))
        return false;

    const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    EmbeddedFrame decoded;
    decoded.width = width;
    decoded.height = height;
    decoded.luma.resize(count);
    const auto byteCount = static_cast<int>(count);
    if(decompressor.read(decoded.luma.data(), byteCount) != byteCount)
        return false;

    frame = std::move(decoded);
    return true;
}
} // namespace spectrumming::plugin
