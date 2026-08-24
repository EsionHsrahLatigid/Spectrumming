#pragma once

#include "BridgeInterfaces.h"

#if defined(SPECTRUMMING_BRIDGE_ENABLE_JUCE_CAMERA) && \
    (defined(__APPLE__) || defined(_WIN32))

#include <juce_video/juce_video.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace spectrumming::bridge {

class JuceCameraFrameSource final : private juce::CameraDevice::Listener {
public:
    static juce::StringArray getAvailableDevices() {
        return juce::CameraDevice::getAvailableDevices();
    }

    JuceCameraFrameSource(IFrameSink& sink, std::uint64_t streamId)
        : sink_(sink), streamId_(streamId) {}

    ~JuceCameraFrameSource() override {
        stop();
    }

    bool start(int cameraIndex = 0,
               int minWidth = 640,
               int minHeight = 480,
               int maxWidth = 1920,
               int maxHeight = 1080) {
        if (device_ != nullptr) {
            return true;
        }

        device_.reset(juce::CameraDevice::openDevice(cameraIndex, minWidth, minHeight,
                                                     maxWidth, maxHeight));
        if (device_ == nullptr) {
            return false;
        }

        device_->addListener(this);
        return true;
    }

    void stop() {
        if (device_ != nullptr) {
            device_->removeListener(this);
            device_.reset();
        }
    }

    std::uint64_t getPublishedFrameCount() const noexcept {
        return publishedFrames_.load(std::memory_order_relaxed);
    }

private:
    void imageReceived(const juce::Image& image) override {
        const auto bounds = image.getBounds();
        if (bounds.isEmpty()) {
            return;
        }

        const auto scale = juce::jmin(1.0,
            juce::jmin(1024.0 / static_cast<double>(bounds.getWidth()),
                       512.0 / static_cast<double>(bounds.getHeight())));
        const auto width = juce::jmax(1, juce::roundToInt(bounds.getWidth() * scale));
        const auto height = juce::jmax(1, juce::roundToInt(bounds.getHeight() * scale));
        const auto normalized = image.rescaled(width, height, juce::Graphics::mediumResamplingQuality);
        scratch_.resize(static_cast<std::size_t>(width * height));
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const auto colour = normalized.getPixelAt(x, y);
                const auto luma = colour.getFloatAlpha()
                                * (0.2126f * colour.getFloatRed()
                                   + 0.7152f * colour.getFloatGreen()
                                   + 0.0722f * colour.getFloatBlue());
                scratch_[static_cast<std::size_t>(y * width + x)] =
                    static_cast<std::uint8_t>(juce::roundToInt(255.0f * juce::jlimit(0.0f, 1.0f, luma)));
            }
        }

        const auto ticks = juce::Time::getHighResolutionTicks();
        const auto ticksPerSecond = juce::Time::getHighResolutionTicksPerSecond();
        const auto timestamp = ticksPerSecond > 0
            ? static_cast<std::uint64_t>((static_cast<long double>(ticks) * 1000000000.0L)
                                         / static_cast<long double>(ticksPerSecond))
            : 0U;

        const auto header = makeFrameHeader(static_cast<std::uint32_t>(width),
                                            static_cast<std::uint32_t>(height),
                                            static_cast<std::uint32_t>(width),
                                            PixelFormat::gray8,
                                            nextFrameId_.fetch_add(1u),
                                            timestamp,
                                            streamId_);
        if (sink_.submitFrame(FrameView {
                header,
                scratch_.data(),
                scratch_.size(),
            }) == ValidationCode::ok) {
            publishedFrames_.fetch_add(1u, std::memory_order_relaxed);
        }
    }

    IFrameSink& sink_;
    std::uint64_t streamId_ = 0;
    std::atomic<std::uint64_t> nextFrameId_ { 1 };
    std::atomic<std::uint64_t> publishedFrames_ { 0 };
    std::vector<std::uint8_t> scratch_;
    std::unique_ptr<juce::CameraDevice> device_;
};

} // namespace spectrumming::bridge

#endif
