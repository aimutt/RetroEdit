#pragma once
#include "Color.h"
#include <string>

// Visual color palette used by every render pass. One instance per
// Application; Render() re-reads from it every frame so changes apply
// immediately.
//
// The struct is data-only; presets live in Theme.cpp and are constructed
// via MakeTheme(). Defaults match the historical retro-green look so
// code paths that default-construct a Theme (e.g. for testing) still get
// a sensible palette.
struct Theme
{
    Color background;
    Color normalText;
    Color dimText;
    Color brightText;
    Color reverseForeground;
    Color reverseBackground;
    Color border;

    // Misspelled words: an amber-orange that contrasts against the green
    // palette without clashing with selection's reverse-video look. White
    // theme uses red instead.
    Color misspelledText;
};

enum class ThemeName
{
    Green = 0,    // default retro green-on-black look
    White,        // black-on-near-white "boring office" look
    Count_,
};

// Build a fresh Theme for the named preset. Both products call this in
// LoadGlobalSettings before the first Render(), so the first frame
// reflects the persisted user choice.
Theme MakeTheme(ThemeName name);

// Convert between the config.ini string key and the enum. Unrecognized
// names fall back to Green so a typo never breaks the editor.
ThemeName        ParseThemeName(const std::string& key);
const char*      ThemeNameKey  (ThemeName name);
// Human-readable label shown in the theme picker (e.g., "Green (retro)").
const char*      ThemeDisplayName(ThemeName name);
inline int       ThemeCount() { return static_cast<int>(ThemeName::Count_); }
