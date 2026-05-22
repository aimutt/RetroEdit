#pragma once
#include <cstdint>

// Per-character text style bits. Stored in CharFormat::style. Values
// intentionally match SDL_ttf's TTF_STYLE_* constants so the bitmask can
// be passed straight into TTF_SetFontStyle / GlyphCache without remapping.
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

// Per-character formatting record: one byte each for style bits, font
// face override, and font size override. Stored in FormattedTextBuffer's
// parallel m_formats vector — exactly one CharFormat per character byte
// of the inner TextBuffer.
//
// `face` and `size` are FontFace / FontSize enum indices, with the
// sentinel value `Inherit` (0xFF) meaning "use the document default
// (Application::m_fontSettings)". Newly-typed characters get the sentinel
// so they automatically follow whatever face/size the user picks via the
// Font dialog without a selection. Selection-targeted Font dialog applies
// real index values that pin the run to a specific face/size.
struct CharFormat
{
    static constexpr uint8_t Inherit = 0xFF;
    uint8_t style = 0;            // CharStyle bits
    uint8_t face  = Inherit;      // FontFace enum index, or Inherit
    uint8_t size  = Inherit;      // FontSize enum index, or Inherit

    bool IsPlain() const { return style == 0 && face == Inherit && size == Inherit; }
};
