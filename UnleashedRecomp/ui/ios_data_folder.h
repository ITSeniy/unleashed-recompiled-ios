#pragma once

#include <filesystem>

// An optional data folder outside the app container, chosen in the launcher. iOS
// deletes the container when an app is removed, which happens when a build signed
// by another team must replace the installed one; a folder in Files survives that.
namespace ios_data_folder
{
    // The chosen folder with access started for the whole process, or empty to use
    // the app's Documents folder. Called once, before anything uses the user path.
    std::filesystem::path Resolve();

    // A folder was chosen but could not be opened (deleted, renamed or on a missing
    // volume); the app fell back to its Documents folder for this session.
    bool IsUnavailable();
}
