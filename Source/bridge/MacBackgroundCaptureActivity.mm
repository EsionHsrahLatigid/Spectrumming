#include "BackgroundCaptureActivity.h"

#import <Foundation/Foundation.h>

namespace spectrumming::bridge
{
void* beginBackgroundCaptureActivity()
{
    @autoreleasepool
    {
        auto* token = [[NSProcessInfo processInfo]
            beginActivityWithOptions:NSActivityUserInitiatedAllowingIdleSystemSleep
                              reason:@"Spectrumming live camera capture"];
        return [token retain];
    }
}

void endBackgroundCaptureActivity(void* token)
{
    if(token == nullptr)
        return;

    @autoreleasepool
    {
        id activity = static_cast<id>(token);
        [[NSProcessInfo processInfo] endActivity:activity];
        [activity release];
    }
}
} // namespace spectrumming::bridge
