#pragma once

#include <filesystem>
#include <string>
#include <vector>

// iOS front-end for the native ModLoader: finds HMM/UMM mod.ini files in the mods
// folder and writes the cpkredir.ini/ModsDB.ini pair that ModLoader::Init() reads,
// with paths relative to the ini files so they survive app container moves.
struct IOSMod
{
    std::filesystem::path iniPath;
    std::string title;
    std::string author;
    std::string version;
    std::filesystem::path schemaPath;
    bool enabled{};
};

namespace ios_mods
{
    std::filesystem::path GetModsDirectory();

    // Active mods first in their saved order, then the rest alphabetically.
    std::vector<IOSMod> Load();

    bool Save(const std::vector<IOSMod>& mods, std::string& error);

    // Copies a mod folder or extracts a ZIP into the mods folder. Returns the number
    // of mods found in it; on failure returns 0 and leaves nothing behind.
    int Install(const std::filesystem::path& source, std::string& error);
}
