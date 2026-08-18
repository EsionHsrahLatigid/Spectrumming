#pragma once

#include "TripleBufferFrameStore.h"

#include <cstdint>
#include <vector>

namespace spectrumming::bridge {

class MockFrameTransport final : public IFrameTransport {
public:
    ValidationCode submitFrame(const FrameView& frame) override {
        return store_.submitFrame(frame);
    }

    bool readLatestFrame(FrameStorage& destination) const override {
        return store_.readLatestFrame(destination);
    }

    std::uint64_t publishCount() const {
        return store_.publishCount();
    }

    ValidationCode submitPixels(const FrameHeader& header,
                                const std::vector<std::uint8_t>& pixels) {
        return submitFrame(FrameView { header, pixels.data(), pixels.size() });
    }

private:
    TripleBufferFrameStore store_;
};

} // namespace spectrumming::bridge
