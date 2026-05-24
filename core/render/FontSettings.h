#pragma once
#include "FontFace.h"
#include <string>

enum class FontSize { Small, Medium, Large, ExtraLarge };

struct FontSettings
{
    FontFace face = FontFace::CascadiaMono;
    FontSize size = FontSize::Medium;

    bool operator==(const FontSettings& o) const
    {
        return face == o.face && size == o.size;
    }
};

inline int FontSizePoints(FontSize size)
{
    switch (size)
    {
        case FontSize::Small:      return 12;
        case FontSize::Medium:     return 16;
        case FontSize::Large:      return 20;
        case FontSize::ExtraLarge: return 24;
    }
    return 16;
}

inline const char* FontSizeName(FontSize size)
{
    switch (size)
    {
        case FontSize::Small:      return "12 pt";
        case FontSize::Medium:     return "16 pt";
        case FontSize::Large:      return "20 pt";
        case FontSize::ExtraLarge: return "24 pt";
    }
    return "16 pt";
}

inline int FontSizeCount() { return 4; }

inline FontSize FontSizeAt(int idx)
{
    switch (idx)
    {
        case 0: return FontSize::Small;
        case 1: return FontSize::Medium;
        case 2: return FontSize::Large;
        case 3: return FontSize::ExtraLarge;
    }
    return FontSize::Medium;
}

inline int IndexOfFontSize(FontSize s)
{
    switch (s)
    {
        case FontSize::Small:      return 0;
        case FontSize::Medium:     return 1;
        case FontSize::Large:      return 2;
        case FontSize::ExtraLarge: return 3;
    }
    return 1;
}

inline std::string ComposePresetLabel(FontFace face, FontSize size)
{
    std::string s = FontFaceName(face);
    s += " - ";
    s += FontSizeName(size);
    return s;
}
