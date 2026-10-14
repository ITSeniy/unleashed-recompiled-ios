#include "app.h"
#include <api/SWA.h>
#include <gpu/video.h>
#include <install/installer.h>
#include <kernel/function.h>
#include <os/process.h>
#include <os/logger.h>
#include <os/app_lifecycle.h>
#include <patches/audio_patches.h>
#include <patches/inspire_patches.h>
#include <ui/game_window.h>
#include <user/config.h>
#include <user/paths.h>
#include <user/registry.h>
#include <os/logger.h>

#include <chrono>
#include <thread>
#include <user/launch_request.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

void App::Restart(std::vector<std::string> restartArgs)
{
#if defined(__APPLE__) && TARGET_OS_IPHONE
    auto action = launch_request::Action::None;
    for (const auto& arg : restartArgs)
    {
        if (arg == "--install-dlc")
            action = launch_request::Action::InstallDLC;
        else if (arg == "--install")
            action = launch_request::Action::Install;
        else
        {
            LOGFN_ERROR("Unsupported iOS restart argument: {}", arg);
            return;
        }
    }
    std::error_code error;
    if (!launch_request::Save(GetUserPath(), action, error))
    {
        LOGFN_ERROR("Failed to save next-launch action: {}", error.message());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, GameWindow::GetTitle(), "Could not save the restart request. Check available storage and try again.", GameWindow::s_pWindow);
        return;
    }
    LOGN("iOS restart requested; settings and installer action will apply on the next home-screen launch.");
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, GameWindow::GetTitle(), "Open Unleashed again from the home screen to continue.", GameWindow::s_pWindow);
#else
    os::process::StartProcess(os::process::GetExecutablePath(), restartArgs, os::process::GetWorkingDirectory());
#endif
    Exit();
}

void App::Exit()
{
    Config::Save();

#ifdef _WIN32
    timeEndPeriod(1);
#endif

    std::_Exit(0);
}

// SWA::CApplication::CApplication
PPC_FUNC_IMPL(__imp__sub_824EB490);
PPC_FUNC(sub_824EB490)
{
    App::s_isInit = true;
    App::s_isMissingDLC = !Installer::checkAllDLC(GetGamePath());
    App::s_language = Config::Language;

    SWA::SGlobals::Init();
    Registry::Save();

    __imp__sub_824EB490(ctx, base);
}

static std::thread::id g_mainThreadId = std::this_thread::get_id();

// SWA::CApplication::Update
PPC_FUNC_IMPL(__imp__sub_822C1130);
PPC_FUNC(sub_822C1130)
{
#ifdef __ANDROID__
    // Freeze the game tick while backgrounded. The flag is driven by the native-window
    // watcher thread in sdl2_driver.cpp (AndroidWindowWatcherThread): the OS destroys
    // the ANativeWindow on background and recreates it on restore - the only lifecycle
    // signal that proved reliable here. SDL's pause/resume events are informational only.
    // NOTE: Audio is paused/resumed by the same watcher via Unleashed_AppSetPaused.
    const bool isMainThread = std::this_thread::get_id() == g_mainThreadId;
    bool wasPaused = App::s_androidPaused.load(std::memory_order_acquire);

    while (App::s_androidPaused.load(std::memory_order_acquire))
    {
        // Non-blocking on Android (SDL_HINT_ANDROID_BLOCK_ON_PAUSE=0), so SDL cannot
        // park this thread inside its resume semaphore.
        if (isMainThread)
            SDL_PumpEvents();

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (isMainThread && wasPaused)
    {
        os::logger::Log("game tick resumed from background - triggering video swapchain check", os::logger::ELogType::Utility, "android");
        Video::OnAndroidResume();  // will invalidate/recreate swapchain if needed
    }
#endif

#if defined(UNLEASHED_RECOMP_IOS)
    app_lifecycle::WaitForFrame(ctx.f1.f64,
        Config::FPS >= FPS_MIN && Config::FPS < FPS_MAX ? 1.0 / Config::FPS : 1.0 / 60.0);
#endif
    Video::WaitOnSwapChain();

    // Correct small delta time errors.
    if (Config::FPS >= FPS_MIN && Config::FPS < FPS_MAX)
    {
        double targetDeltaTime = 1.0 / Config::FPS;

        if (abs(ctx.f1.f64 - targetDeltaTime) < 0.00001)
            ctx.f1.f64 = targetDeltaTime;
    }

    App::s_deltaTime = ctx.f1.f64;
    App::s_time += App::s_deltaTime;

#ifdef __ANDROID__
    // Publish the actual internal stage name into log.txt. Empty names cover the
    // title screen, menus, world map and transitions where no playable stage exists.
    const char* testLocation = "front_end_or_loading";
    if (auto pGameDocument = SWA::CGameDocument::GetInstance())
    {
        if (pGameDocument->m_pMember)
        {
            const char* stageName = pGameDocument->m_pMember->m_StageName.c_str();
            if (stageName != nullptr && stageName[0] != '\0')
                testLocation = stageName;
        }
    }
    os::logger::SetTestLocation(testLocation);
#endif

    // This function can also be called by the loading thread,
    // which SDL does not like. To prevent the OS from thinking
    // the process is unresponsive, we will flush while waiting
    // for the pipelines to finish compiling in video.cpp.
    if (std::this_thread::get_id() == g_mainThreadId)
    {
        SDL_PumpEvents();
        // Do not SDL_FlushEvents here: it discards SDL_APP_DIDENTERBACKGROUND before SDL's
        // Android pause machinery can finish, and the game loop would keep running in background.
        GameWindow::Update();
    }

    AudioPatches::Update(App::s_deltaTime);
    InspirePatches::Update();

    // Apply subtitles option.
    if (auto pApplicationDocument = SWA::CApplicationDocument::GetInstance())
        pApplicationDocument->m_InspireSubtitles = Config::Subtitles;

    if (Config::EnableEventCollisionDebugView)
        *SWA::SGlobals::ms_IsTriggerRender = true;

    if (Config::EnableGIMipLevelDebugView)
        *SWA::SGlobals::ms_VisualizeLoadedLevel = true;

    if (Config::EnableObjectCollisionDebugView)
        *SWA::SGlobals::ms_IsObjectCollisionRender = true;

    if (Config::EnableStageCollisionDebugView)
        *SWA::SGlobals::ms_IsCollisionRender = true;

    __imp__sub_822C1130(ctx, base);
}
