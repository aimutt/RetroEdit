#include "WysiwygRenderer.h"
#include "render/GlyphCache.h"
#include "editor/TextBuffer.h"
#include "editor/FormattedTextBuffer.h"
#include "platform/AssetPath.h"

#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <string>
#include <vector>

namespace
{
    // US Letter, hardcoded for Phase 1.
    constexpr double kPaperWidthIn  = 8.5;
    constexpr double kPaperHeightIn = 11.0;

    void FillRect(SDL_Renderer* r, int x, int y, int w, int h, Color c) {
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        SDL_FRect rect{ static_cast<float>(x), static_cast<float>(y),
                        static_cast<float>(w), static_cast<float>(h) };
        SDL_RenderFillRect(r, &rect);
    }

    void StrokeRect(SDL_Renderer* r, int x, int y, int w, int h, Color c) {
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        SDL_FRect rect{ static_cast<float>(x), static_cast<float>(y),
                        static_cast<float>(w), static_cast<float>(h) };
        SDL_RenderRect(r, &rect);
    }

    // Word-aware wrap by COLUMN count (not pixel width). For monospace
    // fonts this gives identical wrap positions to Print.cpp's WrapLine,
    // making display and print line up exactly. Empty source line produces
    // one empty segment so it still consumes a row.
    std::vector<std::string> WrapByColumns(const std::string& src, int charsPerLine)
    {
        std::vector<std::string> out;
        if (charsPerLine <= 0) { out.push_back(src); return out; }
        if (src.empty())       { out.emplace_back();  return out; }

        size_t i = 0;
        const size_t n = src.size();
        while (i < n)
        {
            size_t start   = i;
            size_t lastBrk = std::string::npos;
            size_t taken   = 0;

            while (i < n && taken < static_cast<size_t>(charsPerLine))
            {
                char c = src[i];
                if (c == ' ' || c == '\t') lastBrk = i;
                ++i;
                ++taken;
            }

            if (i < n && lastBrk != std::string::npos && lastBrk > start)
            {
                out.emplace_back(src.substr(start, lastBrk - start));
                i = lastBrk + 1; // skip the space
            }
            else
            {
                out.emplace_back(src.substr(start, i - start));
            }
        }
        return out;
    }
}

namespace
{
    constexpr int kPageGapPx = 16; // empty space between stacked page rects
}

int WysiwygRenderer::ClampScrollForCursor(const DrawContext& ctx)
{
    if (!ctx.buffer) return ctx.viewportTopPx;
    const int dpi = std::max(48, ctx.screenDpi);
    EnsureFont(ctx.face, ctx.pointSize, dpi);
    if (!m_glyphs || !m_glyphs->IsValid()) return ctx.viewportTopPx;

    const int charsPerLine = ComputeCharsPerLine(
        ctx.face, ctx.pointSize, ctx.margins.leftIn, ctx.margins.rightIn);
    const int lh        = m_glyphs->LineHeight();
    const int pageH     = static_cast<int>(kPaperHeightIn * dpi);
    const int mTopPx    = static_cast<int>(ctx.margins.topIn    * dpi);
    const int mBottomPx = static_cast<int>(ctx.margins.bottomIn * dpi);
    const int usableH   = std::max(lh, pageH - mTopPx - mBottomPx);
    const int linesPerPage = std::max(1, usableH / lh);
    const int pageStride   = pageH + kPageGapPx;

    int row = std::clamp(ctx.cursorRow, 0, std::max(0, ctx.buffer->LineCount() - 1));

    // Visual-line index of (cursorRow, cursorCol) across the whole document.
    int visualLine = 0;
    for (int li = 0; li < row; ++li)
    {
        auto segs = WrapByColumns(ctx.buffer->Line(li), charsPerLine);
        visualLine += static_cast<int>(segs.size());
    }
    if (row < ctx.buffer->LineCount())
    {
        const std::string& line = ctx.buffer->Line(row);
        auto segs = WrapByColumns(line, charsPerLine);
        int cum = 0;
        for (size_t i = 0; i < segs.size(); ++i)
        {
            int segLen = static_cast<int>(segs[i].size());
            int segEnd = cum + segLen;
            bool isLast = (i + 1 == segs.size());
            if (ctx.cursorCol <= segEnd || isLast)
            {
                visualLine += static_cast<int>(i);
                break;
            }
            cum = segEnd;
            if (cum < static_cast<int>(line.size()) && line[cum] == ' ')
                cum += 1;
        }
    }

    // Paginated Y: each page occupies (pageH + gap), text starts at top
    // margin within its page.
    int pageIdx    = visualLine / linesPerPage;
    int lineOnPage = visualLine % linesPerPage;
    int cursorY    = pageIdx * pageStride + mTopPx + lineOnPage * lh;

    // Page-snap: when the editor area can fit the whole page rectangle,
    // align the cursor's page to the top of the editor area so the user
    // always sees a complete page (rather than the tail of one and the
    // head of the next). When the cursor crosses to the next page the
    // viewport jumps a full page-stride.
    if (ctx.editorAreaPxH >= pageH)
        return pageIdx * pageStride;

    // Fallback for tall pages in short windows: keep the cursor visible
    // by scrolling line-by-line.
    int viewport = ctx.viewportTopPx;
    if (cursorY < viewport)
        viewport = cursorY;
    if (cursorY + lh > viewport + ctx.editorAreaPxH)
        viewport = cursorY + lh - ctx.editorAreaPxH;
    if (viewport < 0) viewport = 0;
    return viewport;
}

int WysiwygRenderer::ComputeCharsPerLine(FontFace face, int ptSize,
                                          double leftMarginIn, double rightMarginIn)
{
    double usableIn = kPaperWidthIn - leftMarginIn - rightMarginIn;
    if (usableIn <= 0.0) return 1;

    // Open the font at a high reference size (1000 px) and read the advance
    // for 'M'. emRatio = advance / refSize is the font's per-em advance
    // ratio at sub-pixel precision. This avoids the integer-rounding loss
    // that happens when the actual screen pixel size is small (e.g. 21 px).
    std::string path = ResolveAssetPath(FontFaceFile(face));
    constexpr float kRefSize = 1000.0f;
    TTF_Font* hires = TTF_OpenFont(path.c_str(), kRefSize);
    double emRatio = 0.6; // sensible monospace fallback (~Cascadia)
    if (hires)
    {
        int minx, maxx, miny, maxy, adv;
        if (TTF_GetGlyphMetrics(hires, U'M', &minx, &maxx, &miny, &maxy, &adv)
            && adv > 0)
        {
            emRatio = adv / static_cast<double>(kRefSize);
        }
        TTF_CloseFont(hires);
    }

    // Each character at ptSize occupies (ptSize * emRatio) points, which is
    // (ptSize * emRatio / 72) inches — same on screen and on paper.
    double charIn = ptSize * emRatio / 72.0;
    if (charIn <= 0.0) return 1;
    int cpl = static_cast<int>(usableIn / charIn);
    return std::max(1, cpl);
}

WysiwygRenderer::WysiwygRenderer(SDL_Renderer* sdl, const Theme& theme)
    : m_sdl(sdl), m_theme(theme)
{
}

WysiwygRenderer::~WysiwygRenderer() = default;

void WysiwygRenderer::EnsureFont(FontFace face, int pointSize, int dpi)
{
    if (m_glyphs && face == m_lastFace
        && pointSize == m_lastPointSize && dpi == m_lastDpi)
        return;

    // Open the font at the true physical pixel size: pointSize * dpi / 72.
    // This is the same conversion the printer applies when CreateFont uses
    // -MulDiv(pointSize, printerDpi, 72), so on-screen layout matches the
    // printed page's chars-per-line.
    int pxSize = std::max(1, (pointSize * dpi + 36) / 72); // +36 = round to nearest
    m_glyphs = std::make_unique<GlyphCache>(m_sdl, face, pxSize);
    m_lastFace      = face;
    m_lastPointSize = pointSize;
    m_lastDpi       = dpi;
}

void WysiwygRenderer::Draw(const DrawContext& ctx)
{
    if (!m_sdl || !ctx.buffer) return;
    EnsureFont(ctx.face, ctx.pointSize, std::max(48, ctx.screenDpi));
    if (!m_glyphs || !m_glyphs->IsValid()) return;

    const int dpi    = std::max(48, ctx.screenDpi);
    const int pageW  = static_cast<int>(kPaperWidthIn  * dpi);
    const int pageH  = static_cast<int>(kPaperHeightIn * dpi);

    // Center horizontally; clip from the left when the page is wider than
    // the editor area (Phase 1: no horizontal scroll).
    int pageX = ctx.editorAreaPxX + (ctx.editorAreaPxW - pageW) / 2;
    if (pageX < ctx.editorAreaPxX) pageX = ctx.editorAreaPxX;
    const int viewport = ctx.viewportTopPx;

    SDL_Rect oldClip{}, newClip{
        ctx.editorAreaPxX, ctx.editorAreaPxY,
        ctx.editorAreaPxW, ctx.editorAreaPxH
    };
    SDL_GetRenderClipRect(m_sdl, &oldClip);
    bool hadClip = (oldClip.w > 0 && oldClip.h > 0);
    SDL_SetRenderClipRect(m_sdl, &newClip);

    FillRect(m_sdl,
             ctx.editorAreaPxX, ctx.editorAreaPxY,
             ctx.editorAreaPxW, ctx.editorAreaPxH,
             m_theme.background);

    const int mTop    = static_cast<int>(ctx.margins.topIn    * dpi);
    const int mBottom = static_cast<int>(ctx.margins.bottomIn * dpi);
    const int mLeft   = static_cast<int>(ctx.margins.leftIn   * dpi);
    const int mRight  = static_cast<int>(ctx.margins.rightIn  * dpi);

    const int usableW       = std::max(1, pageW - mLeft - mRight);
    const int usableH       = std::max(1, pageH - mTop  - mBottom);
    const int lh            = m_glyphs->LineHeight();
    const int linesPerPage  = std::max(1, usableH / lh);
    const int pageStride    = pageH + kPageGapPx;

    // DPI-independent wrap: same chars-per-line as the printer will use.
    const int charsPerLine = ComputeCharsPerLine(
        ctx.face, ctx.pointSize, ctx.margins.leftIn, ctx.margins.rightIn);

    // Pre-pass: pre-wrap every buffer line and total the visual-line count.
    // We need this for total page count up front; the wrap result is also
    // reused by the text pass below so we don't pay it twice.
    std::vector<std::vector<std::string>> wrapped;
    wrapped.reserve(ctx.buffer->LineCount());
    int totalVisualLines = 0;
    for (int li = 0; li < ctx.buffer->LineCount(); ++li)
    {
        wrapped.emplace_back(WrapByColumns(ctx.buffer->Line(li), charsPerLine));
        totalVisualLines += static_cast<int>(wrapped.back().size());
    }
    const int totalPages = std::max(1,
        (totalVisualLines + linesPerPage - 1) / linesPerPage);

    // ----- Pass A: paint page rectangles + margin guides for visible pages.
    Color paper = m_theme.background;
    paper.r = static_cast<uint8_t>(std::min(255, paper.r + 18));
    paper.g = static_cast<uint8_t>(std::min(255, paper.g + 32));
    paper.b = static_cast<uint8_t>(std::min(255, paper.b + 18));

    for (int p = 0; p < totalPages; ++p)
    {
        int pageTopY = ctx.editorAreaPxY - viewport + p * pageStride;
        if (pageTopY + pageH < ctx.editorAreaPxY) continue;
        if (pageTopY > ctx.editorAreaPxY + ctx.editorAreaPxH) break;

        FillRect  (m_sdl, pageX, pageTopY, pageW, pageH, paper);
        StrokeRect(m_sdl, pageX, pageTopY, pageW, pageH, m_theme.border);
        StrokeRect(m_sdl, pageX + mLeft, pageTopY + mTop, usableW, usableH,
                   m_theme.dimText);
    }

    // ----- Pass B: paint text, paginated.
    auto selRange = [&](int& sr, int& sc, int& er, int& ec) {
        sr = ctx.selAnchorRow; sc = ctx.selAnchorCol;
        er = ctx.cursorRow;    ec = ctx.cursorCol;
        if (sr > er || (sr == er && sc > ec)) {
            std::swap(sr, er);
            std::swap(sc, ec);
        }
    };
    int sr = 0, sc = 0, er = 0, ec = 0;
    if (ctx.selActive) selRange(sr, sc, er, ec);

    // Per-char style lookup. When no FormattedTextBuffer is provided, every
    // character renders as styleBits = 0 (plain — same as Phase 1).
    auto styleAt = [&](int row, int col) -> int {
        return ctx.formatted ? ctx.formatted->StyleAt(row, col) : 0;
    };

    int visualLine = 0;
    for (int li = 0; li < ctx.buffer->LineCount(); ++li)
    {
        const std::string& line = ctx.buffer->Line(li);
        const auto& segs = wrapped[li];

        // Map each wrap segment back to its source-column origin so cursor
        // and selection can be located inside it.
        std::vector<int> segStart;
        segStart.reserve(segs.size());
        int cum = 0;
        for (const auto& s : segs)
        {
            segStart.push_back(cum);
            cum += static_cast<int>(s.size());
            if (cum < static_cast<int>(line.size()) && line[cum] == ' ')
                cum += 1;
        }

        for (size_t segIdx = 0; segIdx < segs.size(); ++segIdx)
        {
            int pageIdx    = visualLine / linesPerPage;
            int lineOnPage = visualLine % linesPerPage;
            int pageTopY   = ctx.editorAreaPxY - viewport + pageIdx * pageStride;
            int textY      = pageTopY + mTop + lineOnPage * lh;
            int usableX    = pageX + mLeft;

            // Cull lines fully outside the editor area.
            if (textY + lh < ctx.editorAreaPxY) { ++visualLine; continue; }
            if (textY > ctx.editorAreaPxY + ctx.editorAreaPxH) goto done;

            {
                const std::string& seg = segs[segIdx];
                int segCol0 = segStart[segIdx];

                if (ctx.selActive && li >= sr && li <= er)
                {
                    int selLo = (li == sr) ? sc : 0;
                    int selHi = (li == er) ? ec : static_cast<int>(line.size());
                    int segLo = segCol0;
                    int segHi = segCol0 + static_cast<int>(seg.size());
                    int hiLo = std::max(selLo, segLo);
                    int hiHi = std::min(selHi, segHi);
                    if (hiHi > hiLo)
                    {
                        int xLo = usableX;
                        for (int c = segLo; c < hiLo && c < segHi; ++c)
                            xLo += m_glyphs->GlyphAdvance(
                                static_cast<char32_t>(static_cast<unsigned char>(line[c])),
                                styleAt(li, c));
                        int xHi = xLo;
                        for (int c = hiLo; c < hiHi; ++c)
                            xHi += m_glyphs->GlyphAdvance(
                                static_cast<char32_t>(static_cast<unsigned char>(line[c])),
                                styleAt(li, c));
                        FillRect(m_sdl, xLo, textY, std::max(2, xHi - xLo), lh,
                                 m_theme.reverseBackground);
                    }
                }

                int x = usableX;
                for (size_t k = 0; k < seg.size(); ++k)
                {
                    char32_t ch = static_cast<char32_t>(static_cast<unsigned char>(seg[k]));
                    int srcCol  = segCol0 + static_cast<int>(k);
                    int style   = styleAt(li, srcCol);
                    int adv = m_glyphs->GlyphAdvance(ch, style);

                    Color fg = m_theme.normalText;
                    if (ctx.selActive)
                    {
                        int colLo  = (li == sr) ? sc : 0;
                        int colHi  = (li == er) ? ec : static_cast<int>(line.size());
                        if (li >= sr && li <= er && srcCol >= colLo && srcCol < colHi)
                            fg = m_theme.reverseForeground;
                    }

                    if (ch > U' ')
                        m_glyphs->DrawGlyphAt(ch, x, textY, fg, style);
                    x += adv;
                }

                if (ctx.cursorVisible && li == ctx.cursorRow)
                {
                    int segLo = segCol0;
                    int segHi = segCol0 + static_cast<int>(seg.size());
                    bool cursorInSegment =
                        (ctx.cursorCol >= segLo && ctx.cursorCol <= segHi)
                        && (segIdx + 1 == segs.size() || ctx.cursorCol < segHi);
                    if (cursorInSegment)
                    {
                        int cx = usableX;
                        for (int c = segLo; c < ctx.cursorCol && c < segHi; ++c)
                            cx += m_glyphs->GlyphAdvance(
                                static_cast<char32_t>(static_cast<unsigned char>(line[c])),
                                styleAt(li, c));
                        FillRect(m_sdl, cx, textY, 2, lh, m_theme.brightText);
                    }
                }
            }

            ++visualLine;
        }
    }
done:

    if (hadClip)
        SDL_SetRenderClipRect(m_sdl, &oldClip);
    else
        SDL_SetRenderClipRect(m_sdl, nullptr);
}
