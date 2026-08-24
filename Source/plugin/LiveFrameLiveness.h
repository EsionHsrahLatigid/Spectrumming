#pragma once

namespace spectrumming::plugin
{
// Live sources publish latest-only frames. Polling at 30 Hz bounds message-thread
// and UI work while keeping preview latency near one conventional video frame.
inline constexpr int liveFramePollRateHz = 30;
inline constexpr double liveFrameSignalTimeoutMs = 1000.0;

class LiveFrameLiveness final
{
public:
    void beginWaiting(const double nowMs) noexcept
    {
        lastFrameTimeMs = nowMs;
        monitoring = true;
    }

    void frameReceived(const double nowMs) noexcept
    {
        lastFrameTimeMs = nowMs;
        monitoring = true;
    }

    bool signalExpired(const double nowMs) const noexcept
    {
        return monitoring && nowMs >= lastFrameTimeMs
            && nowMs - lastFrameTimeMs >= liveFrameSignalTimeoutMs;
    }

private:
    double lastFrameTimeMs = 0.0;
    bool monitoring = false;
};
} // namespace spectrumming::plugin
