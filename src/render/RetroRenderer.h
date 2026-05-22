#pragma once
#include "FontSettings.h"
#include "ScreenBuffer.h"
#include <SDL3/SDL.h>
#include <memory>

class GlyphCache;

class RetroRenderer
{
public:
    RetroRenderer(SDL_Renderer* renderer, const FontSettings& settings);
    ~RetroRenderer();

    // Convenience: PaintBuffer + Present in one call (existing callers).
    void Render(const ScreenBuffer& buffer);

    // Split form used by the WYSIWYG renderer, which needs to draw a
    // proportional overlay AFTER the cell-grid passes but BEFORE the buffer
    // is presented to the window.
    void PaintBuffer(const ScreenBuffer& buffer);
    void Present();

    void                SetFontSettings(const FontSettings& settings);
    const FontSettings& GetFontSettings() const { return m_settings; }
    int                 CellWidth()      const;
    int                 CellHeight()     const;
    GlyphCache*         GlyphCachePtr()  const { return m_glyphs.get(); }
    SDL_Renderer*       SdlRenderer()    const { return m_renderer; }

private:
    SDL_Renderer*               m_renderer;
    FontSettings                m_settings;
    std::unique_ptr<GlyphCache> m_glyphs;
};
