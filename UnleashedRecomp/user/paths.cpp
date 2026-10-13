#include "paths.h"
#include <os/process.h>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <SDL.h>
#endif
#if defined(UNLEASHED_RECOMP_IOS)
#include <ui/ios_data_folder.h>
#endif
#if defined(__linux__) || defined(__APPLE__)
#include <pwd.h>
#include <unistd.h>
#endif

std::filesystem::path g_executableRoot = os::process::GetExecutableRoot();

bool CheckPortable()
{
#if defined(__APPLE__) && TARGET_OS_IPHONE
    // App bundles and their parent containers are signed and read-only on iOS.
    return false;
#else
    std::error_code ec;
    return std::filesystem::exists(g_executableRoot / "portable.txt", ec);
#endif
}

std::filesystem::path BuildUserPath()
{
    if (CheckPortable())
        return g_executableRoot;

    std::filesystem::path userPath;

#if defined(_WIN32)
    PWSTR knownPath = NULL;
    if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, NULL, &knownPath) == S_OK)
        userPath = std::filesystem::path{ knownPath } / USER_DIRECTORY;

    CoTaskMemFree(knownPath);
#elif defined(__ANDROID__)
    // No meaningful $HOME/passwd entry for an app UID; getpwuid()->pw_dir returns a
    // generic path apps can't write to. Keep config/saves next to the game files
    // (legacy internal install or external app storage, whichever GetDataRoot picked).
    userPath = os::android::GetDataRoot() / ".config" / USER_DIRECTORY;
#elif defined(__linux__) || defined(__APPLE__)
#if defined(__APPLE__) && TARGET_OS_IPHONE
#if defined(UNLEASHED_RECOMP_IOS)
    // A folder in Files chosen in the launcher, which survives reinstalling the app.
    if (auto dataFolder = ios_data_folder::Resolve(); !dataFolder.empty())
        return dataFolder;
#endif
    const char* homeDir = getenv("HOME");
    if (homeDir != nullptr && *homeDir != '\0')
    {
        userPath = std::filesystem::path(homeDir) / "Documents" / USER_DIRECTORY;
    }
    else if (char* prefPath = SDL_GetPrefPath("hedge_dev", USER_DIRECTORY); prefPath != nullptr)
    {
        userPath = prefPath;
        SDL_free(prefPath);
    }
#else
    const char* homeDir = getenv("HOME");
    if (homeDir == nullptr)
    {
        if (const passwd* pw = getpwuid(getuid()); pw != nullptr)
            homeDir = pw->pw_dir;
    }

    if (userPath.empty() && homeDir != nullptr)
    {
        // Prefer to store in the .config directory if it exists. Use the home directory otherwise.
        std::filesystem::path homePath = homeDir;
#if defined(__linux__)
        std::filesystem::path configPath = homePath / ".config";
#else
        std::filesystem::path configPath = homePath / "Library" / "Application Support";
#endif
        std::error_code ec;
        if (std::filesystem::exists(configPath, ec))
            userPath = configPath / USER_DIRECTORY;
        else
            userPath = homePath / ("." USER_DIRECTORY);
    }
#endif

    if (userPath.empty())
    {
        std::error_code ec;
        const auto tempPath = std::filesystem::temp_directory_path(ec);
        if (!ec)
            userPath = tempPath / USER_DIRECTORY;
    }
#else
    static_assert(false, "GetUserPath() not implemented for this platform.");
#endif

    return userPath;
}

const std::filesystem::path& GetUserPath()
{
    // Lazy: on Android the path is resolved through SDL/JNI, which isn't available
    // yet when static initializers of this library run.
    static std::filesystem::path userPath = BuildUserPath();
    return userPath;
}
