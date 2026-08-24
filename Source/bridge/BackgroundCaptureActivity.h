#pragma once

namespace spectrumming::bridge
{
#if defined(__APPLE__)
void* beginBackgroundCaptureActivity();
void endBackgroundCaptureActivity(void* token);
#else
inline void* beginBackgroundCaptureActivity() { return nullptr; }
inline void endBackgroundCaptureActivity(void*) {}
#endif
} // namespace spectrumming::bridge
