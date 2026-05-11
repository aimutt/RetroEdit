#include "RetroRenderer.h"
#include "GlyphCache.h"

RetroRenderer::RetroRenderer(SDL_Renderer* renderer, const FontSettings& settings)
    : m_renderer(renderer)
{
    SetFontSettings(settings);
}

RetroRenderer::~RetroRenderer() = default;

void RetroRenderer::SetFontSettings(const FontSettings& settings)
{
    m_settings = settings;
    m_glyphs   = std::make_unique<GlyphCache>(
        m_renderer, settings.face, FontSizePoints(settings.size));
}

int RetroRenderer::CellWidth() const
{
    return m_glyphs ? m_glyphs->CellWidth() : 0;
}

int RetroRenderer::CellHeight() const
{
    return m_glyphs ? m_glyphs->CellHeight() : 0;
}

void RetroRenderer::Render(const ScreenBuffer& buffer)
{
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
    SDL_RenderClear(m_renderer);

    const int cw = CellWidth();
    const int ch = CellHeight();
    if (cw <= 0 || ch <= 0)
    {
        SDL_RenderPresent(m_renderer);
        return;
    }

    // Pass 1: cell backgrounds
    for (int row = 0; row < buffer.Rows(); ++row)
    {
        for (int col = 0; col < buffer.Columns(); ++col)
        {
            const ScreenCell& cell = buffer.At(col, row);
            Color bg = cell.reverseVideo ? cell.foreground : cell.background;

            SDL_FRect rect{
                static_cast<float>(col * cw),
                static_cast<float>(row * ch),
                static_cast<float>(cw),
                static_cast<float>(ch)
            };
            SDL_SetRenderDrawColor(m_renderer, bg.r, bg.g, bg.b, bg.a);
            SDL_RenderFillRect(m_renderer, &rect);
        }
    }

    // Pass 2: glyphs (tinted by foreground colour)
    if (m_glyphs && m_glyphs->IsValid())
    {
        for (int row = 0; row < buffer.Rows(); ++row)
        {
            for (int col = 0; col < buffer.Columns(); ++col)
            {
                const ScreenCell& cell = buffer.At(col, row);
                if (cell.character <= U' ') continue;

                Color fg = cell.reverseVideo ? cell.background : cell.foreground;
                m_glyphs->DrawGlyph(cell.character, col * cw, row * ch, fg);
            }
        }
    }

    SDL_RenderPresent(m_renderer);
}
