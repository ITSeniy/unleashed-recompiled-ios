#pragma once

// Call on UIKit's main thread before starting the guest and returning from main.
bool StartIOSEventLoop();

// May be called from the guest/config thread; UIKit applies it on the next tick.
void SetIOSFrameRateLimit(int frameRate);

// Actual display refresh period in seconds (e.g. 16.64 ms on a nominal 60 Hz
// panel), or 0 before the first display link tick. Safe from any thread.
double GetIOSDisplayRefreshInterval();
