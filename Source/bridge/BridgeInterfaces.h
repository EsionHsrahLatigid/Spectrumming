#pragma once

#include "BridgeFrame.h"

namespace spectrumming::bridge {

class IFrameSink {
public:
    virtual ~IFrameSink() = default;
    virtual ValidationCode submitFrame(const FrameView& frame) = 0;
};

class IFrameSource {
public:
    virtual ~IFrameSource() = default;
    virtual bool readLatestFrame(FrameStorage& destination) const = 0;
};

class IFrameTransport : public IFrameSink, public IFrameSource {
public:
    ~IFrameTransport() override = default;
};

} // namespace spectrumming::bridge
