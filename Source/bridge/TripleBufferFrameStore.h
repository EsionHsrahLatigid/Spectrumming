#pragma once

#include "BridgeInterfaces.h"

#include <array>
#include <cstddef>
#include <mutex>

namespace spectrumming::bridge {

class TripleBufferFrameStore final : public IFrameTransport {
public:
    static constexpr std::size_t kSlotCount = 3;

    ValidationCode submitFrame(const FrameView& frame) override {
        const auto validation = validateFrame(frame);
        if (validation != ValidationCode::ok) {
            return validation;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        const auto nextSlot = (latestSlot_ + 1u) % kSlotCount;
        auto& slot = slots_[nextSlot];
        slot.header = frame.header;
        slot.pixels.assign(frame.pixels, frame.pixels + expectedPixelBytes(frame.header));
        latestSlot_ = nextSlot;
        hasFrame_ = true;
        ++publishCount_;
        return ValidationCode::ok;
    }

    bool readLatestFrame(FrameStorage& destination) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!hasFrame_) {
            return false;
        }

        destination = slots_[latestSlot_];
        return true;
    }

    std::uint64_t publishCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return publishCount_;
    }

private:
    mutable std::mutex mutex_;
    std::array<FrameStorage, kSlotCount> slots_ {};
    std::size_t latestSlot_ = 0;
    bool hasFrame_ = false;
    std::uint64_t publishCount_ = 0;
};

} // namespace spectrumming::bridge
