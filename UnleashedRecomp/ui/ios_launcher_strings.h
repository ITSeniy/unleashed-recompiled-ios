#pragma once

#include <locale/locale.h>
#include <string_view>

// The launcher follows the system language like the Android launcher, including
// Portuguese and Russian, which the game itself does not have.
namespace launcher_strings
{
    enum class Language
    {
        English,
        German,
        Spanish,
        French,
        Italian,
        Japanese,
        Portuguese,
        Russian,
        Count
    };

    // Picks the first supported language from an ISO 639 code list (system order).
    void SetLanguage(std::string_view code);
    Language GetLanguage();

    // The game language matching the launcher language; English for Portuguese and
    // Russian, whose config text comes from GetConfigOverride.
    ELanguage GetGameLanguage();

    // Launcher text in the current language, falling back to English.
    const char* Get(std::string_view key);

    // Launcher text replacing game config text (Name.<config>, Desc.<config>,
    // Value.<config>.<value>) in the current language, or nullptr to keep the game's.
    const char* GetConfigOverride(std::string_view key);
}
