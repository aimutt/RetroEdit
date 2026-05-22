#pragma once
#include "Color.h"
#include "FontFace.h"
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <string>
#include <unordered_map>

// Wraps a single TTF_Font at a given pixel size and caches per-codepoint
// glyph textures. Glyphs are rendered white-on-transparent and tinted at
// draw time via SDL_SetTextureColorMod, so the same texture serves all
// foreground colors.
class GlyphCache
{
public:
    // Loads the given font at `pointSize` from the assets directory. If load
    // fails, IsValid() returns false and the renderer falls back to a 1px
    // background block per glyph.
    GlyphCache(SDL_Renderer* renderer, FontFace face, int pointSize);
    ~GlyphCache();

    GlyphCache(const GlyphCache&)            = delete;
    GlyphCache& operator=(const GlyphCache&) = delete;

    bool IsValid()    const { return m_font != nullptr; }
    int  CellWidth()  const { return m_cellWidth; }
    int  CellHeight() const { return m_cellHeight; }

    // Draws a single glyph centred horizontally inside the cell anchored at
    // (x, y) in window pixels. No-ops on glyphs we can't render.
    void DrawGlyph(char32_t codepoint, int x, int y, Color tint);

    // Proportional layout support (used by WysiwygRenderer). LineHeight is the
    // vertical advance between baselines; GlyphAdvance is the horizontal
    // advance for a single glyph (uniform for monospace, varies for
    // proportional fonts).
    int LineHeight()                       const { return m_cellHeight; }
    int GlyphAdvance(char32_t codepoint)   const;

    // Draws a glyph at exact pixel position (no centering). For proportional
    // rendering — caller has already computed x via summed glyph advances.
    void DrawGlyphAt(char32_t codepoint, int x, int y, Color tint);

private:
    SDL_Texture* GlyphTexture(char32_t codepoint, int& outWidth, int& outHeight);

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
    std::unordered_map<char32_t, CachedGlyph> m_glyphs;
};
