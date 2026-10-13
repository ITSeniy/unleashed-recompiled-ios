#include <stdafx.h>
#include "ios_event_loop.h"
#include <ui/game_window.h>
#include <hid/hid.h>
#include <apu/audio.h>
#include <os/app_lifecycle.h>
#include <os/logger.h>
#include <user/config.h>
#ifdef UNLEASHED_RECOMP_METAL
#include <plume_metal_lifecycle.h>
#endif
#include <algorithm>
#include <atomic>
#include <cstdlib>
#import <QuartzCore/CADisplayLink.h>
#import <UIKit/UIKit.h>

static CADisplayLink* g_displayLink;
static std::atomic<int> g_frameRateLimit{60};
static std::atomic<double> g_displayRefreshInterval{0.0};

static void EnsureIOSWindowScene()
{
    assert(NSThread.isMainThread);
    UIWindow* window = (__bridge UIWindow*)GameWindow::s_renderWindow.window;
    if (window == nil || window.windowScene != nil)
        return;

    // SDL can create its legacy fallback window before UIKit connects the
    // scene. Such a window keeps accepting Metal drawables but never appears.
    // Retry from the display link once the scene becomes available, keeping
    // the existing Metal view and touch controls attached to the same window.
    UIWindowScene* candidate = nil;
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes)
    {
        if (![scene isKindOfClass:UIWindowScene.class])
            continue;
        UIWindowScene* windowScene = (UIWindowScene*)scene;
        if (windowScene.screen != window.screen)
            continue;
        if (scene.activationState == UISceneActivationStateForegroundActive)
        {
            candidate = windowScene;
            break;
        }
        if (scene.activationState == UISceneActivationStateForegroundInactive)
            candidate = windowScene;
    }
    if (candidate == nil)
        return;

    window.windowScene = candidate;
    [window makeKeyAndVisible];
    [window setNeedsLayout];
    [window layoutIfNeeded];
    LOGN("iOS window attached to its foreground scene after SDL fallback creation.");
}

double GetIOSDisplayRefreshInterval()
{
    return g_displayRefreshInterval.load(std::memory_order_relaxed);
}

void SetIOSFrameRateLimit(int frameRate)
{
    g_frameRateLimit.store(frameRate, std::memory_order_relaxed);
}

static void UpdateDisplayLinkFrameRate()
{
    assert(NSThread.isMainThread);
    UIWindow* window = (__bridge UIWindow*)GameWindow::s_renderWindow.window;
    const int maximum = std::max(1, (int)window.screen.maximumFramesPerSecond);
    const int limit = g_frameRateLimit.load(std::memory_order_relaxed);
    const float preferred = limit >= FPS_MIN && limit < FPS_MAX
        ? std::min(limit, maximum) : maximum;

    // Allow the next supported display rate above the software cap (e.g. 120 Hz
    // for a 90 FPS cap). Unlimited rendering requests the display's full rate.
    const CAFrameRateRange range = CAFrameRateRangeMake(preferred, maximum, preferred);
    if (!CAFrameRateRangeIsEqualToRange(g_displayLink.preferredFrameRateRange, range))
    {
        g_displayLink.preferredFrameRateRange = range;
        LOGFN("iOS refresh request: {} FPS, display maximum: {} Hz.", preferred, maximum);
    }
}

static void SetInputFocus(bool focused)
{
    if (GameWindow::s_isFocused == focused)
        return;

    // Use the existing focus handler, including stopping controller rumble.
    SDL_Event event{};
    event.type = SDL_WINDOWEVENT;
    event.window.windowID = SDL_GetWindowID(GameWindow::s_pWindow);
    event.window.event = focused ? SDL_WINDOWEVENT_FOCUS_GAINED : SDL_WINDOWEVENT_FOCUS_LOST;
    SDL_PushEvent(&event);
}

static void PumpEvents(void*)
{
    // SDL disables its nested UIKit run-loop pump after SDL_main returns.
    // SDL_PumpEvents still updates joysticks and dispatches our event watches.
    SetInputFocus(UIApplication.sharedApplication.applicationState == UIApplicationStateActive);
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    hid::Update();
    GameWindow::Update();
}

@interface UnleashedIOSDisplayLinkTarget : NSObject
- (void)tick:(CADisplayLink*)displayLink;
@end

@implementation UnleashedIOSDisplayLinkTarget
- (void)tick:(CADisplayLink*)displayLink
{
    EnsureIOSWindowScene();
    static const bool diagnostics = [] {
        const char* value = std::getenv("UNLEASHED_IOS_FRAME_DIAGNOSTICS");
        return value != nullptr && value[0] == '1';
    }();
    static uint32_t diagnosticTicks = 0;
    if (diagnostics && ++diagnosticTicks % 120 == 0)
    {
        UIWindow* window = (__bridge UIWindow*)GameWindow::s_renderWindow.window;
        UIView* root = window.rootViewController.view;
        CALayer* metal = (__bridge CALayer*)GameWindow::s_renderWindow.view;
        LOGFN("iOS display state: thermal={}, lowPower={}, hidden={}, alpha={:.2f}, key={}, scene={}, attached={}, rootMetal={}, window={:.0f}x{:.0f}, layer={:.0f}x{:.0f}",
            (int)NSProcessInfo.processInfo.thermalState, (bool)NSProcessInfo.processInfo.lowPowerModeEnabled,
            (bool)window.hidden, (double)window.alpha, (bool)window.isKeyWindow, window.windowScene != nil,
            metal.superlayer != nil, root.layer == metal,
            (double)window.bounds.size.width, (double)window.bounds.size.height,
            (double)metal.bounds.size.width, (double)metal.bounds.size.height);
    }
    UpdateDisplayLinkFrameRate();
    PumpEvents(nullptr);
    const double frameInterval = displayLink.targetTimestamp - displayLink.timestamp;
    // The link interval is a whole number of real refreshes; keep one refresh,
    // smoothed because timestamps carry a little scheduling jitter.
    UIWindow* window = (__bridge UIWindow*)GameWindow::s_renderWindow.window;
    const int maximum = std::max(1, (int)window.screen.maximumFramesPerSecond);
    const long refreshes = std::lround(frameInterval * maximum);
    if (frameInterval > 0.0 && refreshes >= 1)
    {
        const double refresh = frameInterval / refreshes;
        const double previous = g_displayRefreshInterval.load(std::memory_order_relaxed);
        g_displayRefreshInterval.store(previous > 0.0 ? previous + (refresh - previous) * 0.05 : refresh,
            std::memory_order_relaxed);
    }
}
@end

static void SetExecutionActive(bool active)
{
    assert(NSThread.isMainThread);
    if (active)
        UpdateDisplayLinkFrameRate();
    g_displayLink.paused = !active;
    auto& gate = app_lifecycle::GetExecutionGate();
    if (gate.IsActive() == active)
        return;

    if (!active)
    {
        gate.SetActive(false);
        XAudioSetAppActive(false);
#ifdef UNLEASHED_RECOMP_METAL
        // This only synchronizes Metal submissions, never guest/UI work.
        // All previously committed buffers must be scheduled before UIKit
        // returns from the background notification.
        plume::setMetalAppActive(false);
#endif
        LOGN("iOS pause requested; audio stopped and Metal submissions drained.");
    }
    else
    {
#ifdef UNLEASHED_RECOMP_METAL
        plume::setMetalAppActive(true);
#endif
        XAudioSetAppActive(true);
        gate.SetActive(true);
        LOGN("iOS execution resumed.");
    }
}

bool StartIOSEventLoop()
{
    assert(NSThread.isMainThread);
    if (g_displayLink != nil)
        return true;

    UIWindow* window = (__bridge UIWindow*)GameWindow::s_renderWindow.window;
    if (window.screen == nil)
    {
        SDL_SetError("No UIKit screen available for the iOS display link");
        return false;
    }
    const char* forceRecovery = std::getenv("UNLEASHED_IOS_FORCE_SCENE_RECOVERY");
    const char* diagnostics = std::getenv("UNLEASHED_IOS_FRAME_DIAGNOSTICS");
    if (forceRecovery != nullptr && forceRecovery[0] == '1' && diagnostics != nullptr && diagnostics[0] == '1')
    {
        window.windowScene = nil;
        LOGN("iOS diagnostic: detached window to exercise scene recovery.");
    }
    EnsureIOSWindowScene();

    g_displayLink = [window.screen displayLinkWithTarget:[UnleashedIOSDisplayLinkTarget new]
        selector:@selector(tick:)];
    if (g_displayLink == nil)
    {
        SDL_SetError("Could not create the iOS display link");
        return false;
    }
    SetIOSFrameRateLimit(Config::FPS);
    UpdateDisplayLinkFrameRate();
    [g_displayLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];

    // SDL also removes its application notification observer when SDL_main
    // returns. Own both focus and execution state while the guest runs independently.
    // UIKit posts these on its main thread. A nil queue delivers synchronously
    // so GPU scheduling finishes before the background notification returns.
    static id inactiveObserver = [NSNotificationCenter.defaultCenter
        addObserverForName:UIApplicationWillResignActiveNotification object:nil
        queue:nil usingBlock:^(NSNotification*) {
            SetInputFocus(false);
            SetExecutionActive(false);
        }];
    static id backgroundObserver = [NSNotificationCenter.defaultCenter
        addObserverForName:UIApplicationDidEnterBackgroundNotification object:nil
        queue:nil usingBlock:^(NSNotification*) {
            SetInputFocus(false);
            SetExecutionActive(false);
        }];
    static id activeObserver = [NSNotificationCenter.defaultCenter
        addObserverForName:UIApplicationDidBecomeActiveNotification object:nil
        queue:nil usingBlock:^(NSNotification*) {
            SetInputFocus(true);
            SetExecutionActive(true);
        }];
    (void)inactiveObserver;
    (void)backgroundObserver;
    (void)activeObserver;

    const bool active = UIApplication.sharedApplication.applicationState == UIApplicationStateActive;
    SetInputFocus(active);
    SetExecutionActive(active);
    return true;
}
