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

    void Render(const ScreenBuffer& buffer);

    void                SetFontSettings(const FontSettings& settings);
    const FontSettings& GetFontSettings() const { return m_settings; }
    int                 CellWidth()      const;
    int                 CellHeight()     const;

private:
    SDL_Renderer*               m_renderer;
    FontSettings                m_settings;
    std::unique_ptr<GlyphCache> m_glyphs;
};
