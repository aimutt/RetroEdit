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
    EBGaramond,
    EBGaramondBold,
    SourceSerif,
    SourceSerifBold,
    SourceSans,
    SourceSansBold,
    OpenSans,
    OpenSansBold,
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
        case FontFace::EBGaramond:          return "fonts/EBGaramond-Regular.ttf";
        case FontFace::EBGaramondBold:      return "fonts/EBGaramond-Bold.ttf";
        case FontFace::SourceSerif:         return "fonts/SourceSerif4-Regular.ttf";
        case FontFace::SourceSerifBold:     return "fonts/SourceSerif4-Bold.ttf";
        case FontFace::SourceSans:          return "fonts/SourceSans3-Regular.ttf";
        case FontFace::SourceSansBold:      return "fonts/SourceSans3-Bold.ttf";
        case FontFace::OpenSans:            return "fonts/OpenSans-Regular.ttf";
        case FontFace::OpenSansBold:        return "fonts/OpenSans-Bold.ttf";
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
        case FontFace::EBGaramond:          return "EB Garamond";
        case FontFace::EBGaramondBold:      return "EB Garamond Bold";
        case FontFace::SourceSerif:         return "Source Serif";
        case FontFace::SourceSerifBold:     return "Source Serif Bold";
        case FontFace::SourceSans:          return "Source Sans";
        case FontFace::SourceSansBold:      return "Source Sans Bold";
        case FontFace::OpenSans:            return "Open Sans";
        case FontFace::OpenSansBold:        return "Open Sans Bold";
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
        case FontFace::EBGaramond:          return "EB Garamond";
        case FontFace::EBGaramondBold:      return "EB Garamond";
        case FontFace::SourceSerif:         return "Source Serif 4";
        case FontFace::SourceSerifBold:     return "Source Serif 4";
        case FontFace::SourceSans:          return "Source Sans 3";
        case FontFace::SourceSansBold:      return "Source Sans 3";
        case FontFace::OpenSans:            return "Open Sans";
        case FontFace::OpenSansBold:        return "Open Sans";
        default:                            return "Cascadia Mono";
    }
}

inline bool FontFaceIsBold(FontFace face)
{
    return face == FontFace::CascadiaMonoBold
        || face == FontFace::JetBrainsMonoBold
        || face == FontFace::IBMPlexMonoBold
        || face == FontFace::EBGaramondBold
        || face == FontFace::SourceSerifBold
        || face == FontFace::SourceSansBold
        || face == FontFace::OpenSansBold;
}

// True for fixed-pitch faces (uniform per-glyph advance). The cell-grid
// chrome and RetroEdit's editor rely on uniform advance; RetroDocWriter's
// WysiwygRenderer measures each glyph independently and works with either.
inline bool FontFaceIsMonospace(FontFace face)
{
    switch (face)
    {
        case FontFace::CascadiaMono:
        case FontFace::CascadiaMonoBold:
        case FontFace::JetBrainsMono:
        case FontFace::JetBrainsMonoBold:
        case FontFace::IBMPlexMono:
        case FontFace::IBMPlexMonoBold:
        case FontFace::VT323:
            return true;
        default:
            return false;
    }
}

inline int FontFaceCount() { return static_cast<int>(FontFace::Count_); }

// Count of monospace faces at the BEGINNING of the enum. RetroEdit's
// cell-grid renderer requires uniform-advance glyphs, so its font dialog
// uses this count (instead of FontFaceCount) to hide proportional faces.
// Assumes monospace faces are contiguous at the start of the enum, which
// the layout above maintains.
inline int FontFaceMonospaceCount()
{
    int n = 0;
    while (n < FontFaceCount() && FontFaceIsMonospace(static_cast<FontFace>(n)))
        ++n;
    return n;
}

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
    if (iequals(name, "Cascadia Mono"))   { out = FontFace::CascadiaMono;  return true; }
    if (iequals(name, "JetBrains Mono"))  { out = FontFace::JetBrainsMono; return true; }
    if (iequals(name, "IBM Plex Mono"))   { out = FontFace::IBMPlexMono;   return true; }
    if (iequals(name, "VT323"))           { out = FontFace::VT323;         return true; }
    if (iequals(name, "EB Garamond"))     { out = FontFace::EBGaramond;    return true; }
    if (iequals(name, "Source Serif 4"))  { out = FontFace::SourceSerif;   return true; }
    if (iequals(name, "Source Serif"))    { out = FontFace::SourceSerif;   return true; }
    if (iequals(name, "Source Sans 3"))   { out = FontFace::SourceSans;    return true; }
    if (iequals(name, "Source Sans"))     { out = FontFace::SourceSans;    return true; }
    if (iequals(name, "Source Sans Pro")) { out = FontFace::SourceSans;    return true; }
    if (iequals(name, "Open Sans"))       { out = FontFace::OpenSans;      return true; }
    return false;
}
