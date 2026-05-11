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

inline int FontFaceCount() { return static_cast<int>(FontFace::Count_); }
