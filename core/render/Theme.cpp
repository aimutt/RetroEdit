#include "Theme.h"
#include <cctype>

namespace
{
    Theme MakeGreen()
    {
        // Historical defaults — preserved byte-for-byte so anyone who
        // hasn't changed their theme sees the same look as before Phase 4.
        Theme t;
        t.background        = {  0,  12,   0, 255};
        t.normalText        = { 80, 255, 120, 255};
        t.dimText           = { 40, 160,  70, 255};
        t.brightText        = {170, 255, 190, 255};
        t.reverseForeground = {  0,  20,   0, 255};
        t.reverseBackground = {100, 255, 130, 255};
        t.border            = { 80, 220, 100, 255};
        t.misspelledText    = {255, 170,  60, 255};
        return t;
    }

    Theme MakeWhite()
    {
        // Office-style black-on-near-white. Background is slightly off-
        // white (240,240,240) to ease eye strain vs pure 255; selection
        // uses the conventional dark-blue / white reverse pair.
        Theme t;
        t.background        = {240, 240, 240, 255};
        t.normalText        = { 20,  20,  20, 255};
        t.dimText           = {120, 120, 120, 255};
        t.brightText        = { 30,  60, 180, 255};
        t.reverseForeground = {250, 250, 250, 255};
        t.reverseBackground = { 60, 110, 200, 255};
        t.border            = {140, 140, 140, 255};
        t.misspelledText    = {200,  50,  50, 255};
        return t;
    }
}

Theme MakeTheme(ThemeName name)
{
    switch (name)
    {
        case ThemeName::Green: return MakeGreen();
        case ThemeName::White: return MakeWhite();
        default:               return MakeGreen();
    }
}

ThemeName ParseThemeName(const std::string& key)
{
    std::string lower;
    lower.reserve(key.size());
    for (char ch : key)
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    if (lower == "white") return ThemeName::White;
    return ThemeName::Green;
}

const char* ThemeNameKey(ThemeName name)
{
    switch (name)
    {
        case ThemeName::White: return "white";
        case ThemeName::Green:
        default:               return "green";
    }
}

const char* ThemeDisplayName(ThemeName name)
{
    switch (name)
    {
        case ThemeName::White: return "White (office)";
        case ThemeName::Green:
        default:               return "Green (retro)";
    }
}
