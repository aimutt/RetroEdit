#pragma once

enum class FontFace
{
    CascadiaMono = 0,
    CascadiaMonoBold,
    JetBrainsMono,
    JetBrainsMonoBold,
    IBMPlexMono,
    IBMPlexMonoBold,
    VT323,
    Count_
};

inline const char* FontFaceFile(FontFace face)
{
    switch (face)
    {
        case FontFace::CascadiaMono:        return "fonts/CascadiaMono-Regular.ttf";
        case FontFace::CascadiaMonoBold:    return "fonts/CascadiaMono-Bold.ttf";
        case FontFace::JetBrainsMono:       return "fonts/JetBrainsMono-Regular.ttf";
        case FontFace::JetBrainsMonoBold:   return "fonts/JetBrainsMono-Bold.ttf";
        case FontFace::IBMPlexMono:         return "fonts/IBMPlexMono-Regular.ttf";
        case FontFace::IBMPlexMonoBold:     return "fonts/IBMPlexMono-Bold.ttf";
        case FontFace::VT323:               return "fonts/VT323-Regular.ttf";
        default:                            return "fonts/CascadiaMono-Regular.ttf";
    }
}

inline const char* FontFaceName(FontFace face)
{
    switch (face)
    {
        case FontFace::CascadiaMono:        return "Cascadia Mono";
        case FontFace::CascadiaMonoBold:    return "Cascadia Mono Bold";
        case FontFace::JetBrainsMono:       return "JetBrains Mono";
        case FontFace::JetBrainsMonoBold:   return "JetBrains Mono Bold";
        case FontFace::IBMPlexMono:         return "IBM Plex Mono";
        case FontFace::IBMPlexMonoBold:     return "IBM Plex Mono Bold";
        case FontFace::VT323:               return "VT323 (CRT)";
        default:                            return "Cascadia Mono";
    }
}

// Windows GDI family name (for CreateFont). Distinct from FontFaceName
// which may include user-facing decoration like " (CRT)".
inline const char* FontFaceFamily(FontFace face)
{
    switch (face)
    {
        case FontFace::CascadiaMono:        return "Cascadia Mono";
        case FontFace::CascadiaMonoBold:    return "Cascadia Mono";
        case FontFace::JetBrainsMono:       return "JetBrains Mono";
        case FontFace::JetBrainsMonoBold:   return "JetBrains Mono";
        case FontFace::IBMPlexMono:         return "IBM Plex Mono";
        case FontFace::IBMPlexMonoBold:     return "IBM Plex Mono";
        case FontFace::VT323:               return "VT323";
        default:                            return "Cascadia Mono";
    }
}

inline bool FontFaceIsBold(FontFace face)
{
    return face == FontFace::CascadiaMonoBold
        || face == FontFace::JetBrainsMonoBold
        || face == FontFace::IBMPlexMonoBold;
}

inline int FontFaceCount() { return static_cast<int>(FontFace::Count_); }

// Reverse lookup: RTF font-table family name → FontFace. Returns true and
// sets `out` on a match; returns false when the family string doesn't
// correspond to any bundled face (the caller can keep its current font).
// Match is case-insensitive on ASCII; trailing/leading whitespace is
// trimmed by the caller before invoking. Bold variants are not selectable
// here — bold is a per-character style bit in RetroDocWriter, not a face.
inline bool FontFaceFromFamilyName(const char* name, FontFace& out)
{
    if (!name) return false;
    auto iequals = [](const char* a, const char* b) {
        while (*a && *b)
        {
            char ca = *a, cb = *b;
            if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca + 32);
            if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb + 32);
            if (ca != cb) return false;
            ++a; ++b;
        }
        return *a == 0 && *b == 0;
    };
    if (iequals(name, "Cascadia Mono")) { out = FontFace::CascadiaMono;  return true; }
    if (iequals(name, "JetBrains Mono")){ out = FontFace::JetBrainsMono; return true; }
    if (iequals(name, "IBM Plex Mono")) { out = FontFace::IBMPlexMono;   return true; }
    if (iequals(name, "VT323"))         { out = FontFace::VT323;         return true; }
    return false;
}
