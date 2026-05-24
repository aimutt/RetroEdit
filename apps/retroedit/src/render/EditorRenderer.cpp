#include "EditorRenderer.h"
#include "render/GlyphCache.h"
#include "editor/TextBuffer.h"
#include "editor/WordWrap.h"
#include "editor/Selection.h"
#include "app/Cursor.h"
#include <algorithm>

namespace
{
    // Match RetroRenderer / WysiwygRenderer's pt → px conversion so a
    // "12 pt" preset renders at the same physical size in all three paths.
    constexpr int kScreenDpi = 96;

    int PixelSizeFor(FontSize size)
    {
        const int pt = FontSizePoints(size);
        return std::max(1, (pt * kScreenDpi + 36) / 72);
    }

    inline SDL_FRect MakeFRect(int x, int y, int w, int h)
    {
        return SDL_FRect{ static_cast<float>(x), static_cast<float>(y),
                          static_cast<float>(w), static_cast<float>(h) };
    }

    inline void FillRectColor(SDL_Renderer* sdl, int x, int y, int w, int h, Color c)
    {
        SDL_SetRenderDrawColor(sdl, c.r, c.g, c.b, c.a);
        SDL_FRect r = MakeFRect(x, y, w, h);
        SDL_RenderFillRect(sdl, &r);
    }
}

EditorRenderer::EditorRenderer(SDL_Renderer* sdl, const Theme& theme,
                                const FontSettings& settings)
    : m_sdl(sdl), m_theme(theme), m_settings(settings)
{
    RebuildGlyphCache();
}

EditorRenderer::~EditorRenderer() = default;

void EditorRenderer::SetFontSettings(const FontSettings& settings)
{
    if (settings == m_settings && m_glyphs) return;
    m_settings = settings;
    RebuildGlyphCache();
}

void EditorRenderer::RebuildGlyphCache()
{
    m_glyphs = std::make_unique<GlyphCache>(
        m_sdl, m_settings.face, PixelSizeFor(m_settings.size));
}

int EditorRenderer::CellWidth() const
{
    return m_glyphs ? m_glyphs->CellWidth() : 0;
}

int EditorRenderer::CellHeight() const
{
    return m_glyphs ? m_glyphs->CellHeight() : 0;
}

bool EditorRenderer::IsValid() const
{
    return m_glyphs && m_glyphs->IsValid();
}

void EditorRenderer::Draw(const DrawContext& ctx)
{
    if (!IsValid() || !ctx.buffer) return;
    const int cw = CellWidth();
    const int ch = CellHeight();
    if (cw <= 0 || ch <= 0) return;
    if (ctx.editorPxW <= 0 || ctx.editorPxH <= 0) return;

    const int editorCols = std::max(1, ctx.editorPxW / cw);
    const int visibleRows = std::max(1, ctx.editorPxH / ch);

    // Clip to the editor pixel rect so partial cells at the right/bottom
    // edge can't overdraw the chrome below.
    SDL_Rect oldClip{};
    bool hadClip = SDL_RenderClipEnabled(m_sdl);
    if (hadClip) SDL_GetRenderClipRect(m_sdl, &oldClip);
    SDL_Rect clip{ ctx.editorPxX, ctx.editorPxY, ctx.editorPxW, ctx.editorPxH };
    SDL_SetRenderClipRect(m_sdl, &clip);

    Selection sel;
    sel.active    = ctx.selActive;
    sel.anchorRow = ctx.selAnchorRow;
    sel.anchorCol = ctx.selAnchorCol;

    auto isMisspelled = [&ctx](int row, int col) {
        for (const auto& s : ctx.misspelledSpans)
            if (s.row == row && col >= s.col && col < s.col + s.len)
                return true;
        return false;
    };

    auto drawCell = [&](int pxX, int pxY, char32_t ch32, bool selected, bool cursor,
                        bool misspell)
    {
        Color fg = m_theme.normalText;
        Color bg = m_theme.background;
        if (selected) { fg = m_theme.reverseForeground; bg = m_theme.reverseBackground; }
        if (cursor)   { std::swap(fg, bg); }  // block cursor inverts the cell
        if (misspell && !selected && !cursor) fg = m_theme.misspelledText;

        FillRectColor(m_sdl, pxX, pxY, cw, ch, bg);
        if (ch32 > U' ' && m_glyphs)
            m_glyphs->DrawGlyph(ch32, pxX, pxY, fg);
    };

    if (!ctx.wordWrap)
    {
        // One buffer row per visual row; viewportLeft slides the column window.
        for (int r = 0; r < visibleRows; ++r)
        {
            const int bufLine = ctx.viewportTop + r;
            if (bufLine < 0 || bufLine >= ctx.buffer->LineCount()) continue;
            const std::string& lineStr = ctx.buffer->Line(bufLine);
            const int lineLen = static_cast<int>(lineStr.size());

            for (int c = 0; c < editorCols; ++c)
            {
                const int bufCol = c + ctx.viewportLeft;
                char32_t ch32 = (bufCol >= 0 && bufCol < lineLen)
                    ? static_cast<char32_t>(static_cast<unsigned char>(lineStr[bufCol]))
                    : U' ';
                bool selected = sel.ContainsCell(bufCol, bufLine,
                                                 ctx.cursorRow, ctx.cursorCol);
                bool cursor = ctx.cursorVisible
                              && bufLine == ctx.cursorRow
                              && bufCol  == ctx.cursorCol;
                bool misspell = (bufCol >= 0 && bufCol < lineLen
                                 && isMisspelled(bufLine, bufCol));
                drawCell(ctx.editorPxX + c * cw, ctx.editorPxY + r * ch,
                         ch32, selected, cursor, misspell);
            }
        }
    }
    else
    {
        // Word-wrap mode: each buffer row may span multiple visual rows.
        int visualRow = 0;
        int bufLine   = ctx.viewportTop;
        while (visualRow < visibleRows && bufLine < ctx.buffer->LineCount())
        {
            const std::string& lineStr = ctx.buffer->Line(bufLine);
            const int lineLen          = static_cast<int>(lineStr.size());
            std::vector<int> starts    = ComputeWrapStarts(lineStr, editorCols);

            for (int seg = 0;
                 seg < static_cast<int>(starts.size()) && visualRow < visibleRows;
                 ++seg, ++visualRow)
            {
                int segStart = starts[seg];
                int segEnd   = (seg + 1 < static_cast<int>(starts.size()))
                                 ? starts[seg + 1]
                                 : lineLen;
                for (int c = 0; c < editorCols; ++c)
                {
                    const int bufCol = segStart + c;
                    bool inSegment   = (bufCol < segEnd);
                    char32_t ch32 = inSegment
                        ? static_cast<char32_t>(static_cast<unsigned char>(lineStr[bufCol]))
                        : U' ';
                    bool selected = inSegment
                                    && sel.ContainsCell(bufCol, bufLine,
                                                        ctx.cursorRow, ctx.cursorCol);
                    bool cursor = ctx.cursorVisible
                                  && bufLine == ctx.cursorRow
                                  && bufCol  == ctx.cursorCol
                                  && inSegment;
                    bool misspell = inSegment && isMisspelled(bufLine, bufCol);
                    drawCell(ctx.editorPxX + c * cw,
                             ctx.editorPxY + visualRow * ch,
                             ch32, selected, cursor, misspell);
                }
            }
            ++bufLine;
        }
    }

    if (hadClip) SDL_SetRenderClipRect(m_sdl, &oldClip);
    else         SDL_SetRenderClipRect(m_sdl, nullptr);
}
