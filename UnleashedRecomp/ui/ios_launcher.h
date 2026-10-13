#pragma once

enum class IOSLauncherAction
{
    Play,
    Install,
    InstallDLC
};

// Shows the native launcher on UIKit's main thread after the configuration is
// loaded and before the game creates its window. Changes are saved to the config.
IOSLauncherAction RunIOSLauncher();
