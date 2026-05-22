#include "GlyphCache.h"
#include "platform/AssetPath.h"

namespace
{
    // Initialise SDL_ttf once for the whole process. We don't bother with a
    // matching TTF_Quit because the OS reclaims everything on exit.
    bool EnsureTtfInitialised()
    {
        static bool initialised = []() {
            return TTF_Init();
        }();
        return initialised;
    }
}

GlyphCache::GlyphCache(SDL_Renderer* renderer, FontFace face, int pointSize)
    : m_renderer(renderer)
{
    if (!EnsureTtfInitialised())
    {
        SDL_Log("TTF_Init failed: %s", SDL_GetError());
        return;
    }

    std::string path = ResolveAssetPath(FontFaceFile(face));
    m_font = TTF_OpenFont(path.c_str(), static_cast<float>(pointSize));
    if (!m_font)
    {
        SDL_Log("TTF_OpenFont failed for %s: %s", path.c_str(), SDL_GetError());
        return;
    }

    // Cell metrics: line height for vertical spacing, advance of 'M' for
    // horizontal. Monospace fonts have a uniform advance, so any printable
    // glyph would do.
    m_cellHeight = TTF_GetFontHeight(m_font);

    int minx = 0, maxx = 0, miny = 0, maxy = 0, advance = 0;
    if (TTF_GetGlyphMetrics(m_font, U'M', &minx, &maxx, &miny, &maxy, &advance))
        m_cellWidth = advance;
    else
        m_cellWidth = m_cellHeight / 2;   // safe fallback
}

GlyphCache::~GlyphCache()
{
    for (auto& [cp, cached] : m_glyphs)
    {
        if (cached.texture) SDL_DestroyTexture(cached.texture);
    }
    if (m_font) TTF_CloseFont(m_font);
}

SDL_Texture* GlyphCache::GlyphTexture(char32_t codepoint, int& outWidth, int& outHeight)
{
    auto it = m_glyphs.find(codepoint);
    if (it != m_glyphs.end())
    {
        outWidth  = it->second.width;
        outHeight = it->second.height;
        return it->second.texture;
    }

    CachedGlyph entry;

    SDL_Color white = { 255, 255, 255, 255 };
    SDL_Surface* surface = TTF_RenderGlyph_Blended(m_font, codepoint, white);
    if (!surface)
    {
        m_glyphs[codepoint] = entry;       // remember the miss too
        outWidth = outHeight = 0;
        return nullptr;
    }

    entry.width   = surface->w;
    entry.height  = surface->h;
    entry.texture = SDL_CreateTextureFromSurface(m_renderer, surface);
    SDL_DestroySurface(surface);

    if (entry.texture)
        SDL_SetTextureScaleMode(entry.texture, SDL_SCALEMODE_LINEAR);

    m_glyphs[codepoint] = entry;
    outWidth  = entry.width;
    outHeight = entry.height;
    return entry.texture;
}

void GlyphCache::DrawGlyph(char32_t codepoint, int x, int y, Color tint)
{
    if (!m_font) return;
    if (codepoint <= U' ') return;        // space and below render as background only

    int gw = 0, gh = 0;
    SDL_Texture* tex = GlyphTexture(codepoint, gw, gh);
    if (!tex) return;

    SDL_SetTextureColorMod(tex, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(tex, tint.a);

    // Centre the glyph horizontally inside the cell; align top of the glyph
    // bitmap with the top of the cell vertically (TTF baseline already accounts
    // for ascent within the surface).
    SDL_FRect dst{
        static_cast<float>(x + (m_cellWidth - gw) / 2),
        static_cast<float>(y),
        static_cast<float>(gw),
        static_cast<float>(gh)
    };
    SDL_RenderTexture(m_renderer, tex, nullptr, &dst);
}

int GlyphCache::GlyphAdvance(char32_t codepoint) const
{
    if (!m_font) return 0;
    int minx, maxx, miny, maxy, advance;
    if (TTF_GetGlyphMetrics(m_font, codepoint, &minx, &maxx, &miny, &maxy, &advance))
        return advance;
    return m_cellWidth; // safe fallback (monospace assumption)
}

void GlyphCache::DrawGlyphAt(char32_t codepoint, int x, int y, Color tint)
{
    if (!m_font) return;
    if (codepoint <= U' ') return;

    int gw = 0, gh = 0;
    SDL_Texture* tex = GlyphTexture(codepoint, gw, gh);
    if (!tex) return;

    SDL_SetTextureColorMod(tex, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(tex, tint.a);

    SDL_FRect dst{
        static_cast<float>(x),
        static_cast<float>(y),
        static_cast<float>(gw),
        static_cast<float>(gh)
    };
    SDL_RenderTexture(m_renderer, tex, nullptr, &dst);
}
