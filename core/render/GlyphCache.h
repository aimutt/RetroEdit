#pragma once
#include "Color.h"
#include "FontFace.h"
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <cstdint>
#include <string>
#include <unordered_map>

// Wraps a single TTF_Font at a given pixel size and caches per-codepoint
// glyph textures. Glyphs are rendered white-on-transparent and tinted at
// draw time via SDL_SetTextureColorMod, so the same texture serves all
// foreground colors.
//
// `styleBits` is an SDL3_ttf TTF_STYLE_* bitmask (Bold/Italic/Underline/
// Strikethrough). The same cache instance handles every style combo by
// keying its glyph map on (codepoint, styleBits) and calling
// TTF_SetFontStyle before rendering each new combo. Callers that don't
// care about styled rendering can omit the parameter — styleBits = 0
// reproduces the original NORMAL behavior, so RetroEdit's call sites
// (cell-grid renderer) are unaffected.
class GlyphCache
{
public:
    GlyphCache(SDL_Renderer* renderer, FontFace face, int pointSize);
    ~GlyphCache();

    GlyphCache(const GlyphCache&)            = delete;
    GlyphCache& operator=(const GlyphCache&) = delete;

    bool IsValid()    const { return m_font != nullptr; }
    int  CellWidth()  const { return m_cellWidth; }
    int  CellHeight() const { return m_cellHeight; }

    // Draws a single glyph centred horizontally inside the cell anchored at
    // (x, y) in window pixels. No-ops on glyphs we can't render.
    void DrawGlyph(char32_t codepoint, int x, int y, Color tint, int styleBits = 0);

    // Proportional layout support (used by WysiwygRenderer). LineHeight is the
    // vertical advance between baselines; GlyphAdvance is the horizontal
    // advance for a single glyph (uniform for monospace, varies for
    // proportional fonts).
    int LineHeight()                                          const { return m_cellHeight; }
    int GlyphAdvance(char32_t codepoint, int styleBits = 0)   const;

    // Draws a glyph at exact pixel position (no centering). For proportional
    // rendering — caller has already computed x via summed glyph advances.
    void DrawGlyphAt(char32_t codepoint, int x, int y, Color tint, int styleBits = 0);

private:
    SDL_Texture* GlyphTexture(char32_t codepoint, int styleBits,
                              int& outWidth, int& outHeight);

    // Cache key packs the codepoint in the high bits and the SDL_ttf style
    // bitmask in the low 8 bits. SDL_ttf style is currently a uint8 (NORMAL,
    // BOLD, ITALIC, UNDERLINE, STRIKETHROUGH all fit in 4 bits).
    static uint64_t MakeKey(char32_t codepoint, int styleBits)
    {
        return (static_cast<uint64_t>(codepoint) << 8) | static_cast<uint8_t>(styleBits & 0xFF);
    }

    SDL_Renderer* m_renderer    = nullptr;
    TTF_Font*     m_font        = nullptr;
    int           m_cellWidth   = 0;
    int           m_cellHeight  = 0;

    struct CachedGlyph
    {
        SDL_Texture* texture = nullptr;
        int          width   = 0;
        int          height  = 0;
    };
    std::unordered_map<uint64_t, CachedGlyph> m_glyphs;
};
