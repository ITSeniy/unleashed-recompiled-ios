#include "ios_launcher_strings.h"
#include <unordered_map>

struct LauncherStringEntry
{
    const char* key;
    const char* text[size_t(launcher_strings::Language::Count)];
};

#include "ios_launcher_strings.inc"

static launcher_strings::Language g_language = launcher_strings::Language::English;

using StringMap = std::unordered_map<std::string_view, const LauncherStringEntry*>;

template<size_t N>
static StringMap BuildMap(const LauncherStringEntry (&entries)[N])
{
    StringMap map;
    for (const auto& entry : entries)
        map.emplace(entry.key, &entry);
    return map;
}

void launcher_strings::SetLanguage(std::string_view code)
{
    static const std::pair<std::string_view, Language> codes[] =
    {
        { "en", Language::English }, { "de", Language::German }, { "es", Language::Spanish },
        { "fr", Language::French }, { "it", Language::Italian }, { "ja", Language::Japanese },
        { "pt", Language::Portuguese }, { "ru", Language::Russian },
    };
    for (const auto& [prefix, language] : codes)
    {
        if (code.substr(0, 2) == prefix)
        {
            g_language = language;
            return;
        }
    }
    g_language = Language::English;
}

launcher_strings::Language launcher_strings::GetLanguage()
{
    return g_language;
}

ELanguage launcher_strings::GetGameLanguage()
{
    switch (g_language)
    {
    case Language::German:   return ELanguage::German;
    case Language::Spanish:  return ELanguage::Spanish;
    case Language::French:   return ELanguage::French;
    case Language::Italian:  return ELanguage::Italian;
    case Language::Japanese: return ELanguage::Japanese;
    default:                 return ELanguage::English;
    }
}

const char* launcher_strings::Get(std::string_view key)
{
    static const StringMap map = BuildMap(g_launcherStrings);
    const auto found = map.find(key);
    if (found == map.end())
        return "";
    const char* text = found->second->text[size_t(g_language)];
    return text ? text : found->second->text[size_t(Language::English)];
}

const char* launcher_strings::GetConfigOverride(std::string_view key)
{
    static const StringMap map = BuildMap(g_configOverrides);
    const auto found = map.find(key);
    return found == map.end() ? nullptr : found->second->text[size_t(g_language)];
}
