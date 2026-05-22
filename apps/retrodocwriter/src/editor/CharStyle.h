#pragma once
#include <cstdint>

// Per-character text style bits. Stored as one byte per character in
// FormattedTextBuffer's parallel style vector. Values intentionally match
// SDL_ttf's TTF_STYLE_* constants so the bitmask can be passed straight
// into TTF_SetFontStyle / GlyphCache without remapping.
namespace CharStyle
{
    enum Bits : uint8_t
    {
        None          = 0x00,
        Bold          = 0x01,
        Italic        = 0x02,
        Underline     = 0x04,
        Strikethrough = 0x08,
    };
}
