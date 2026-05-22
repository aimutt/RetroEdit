#include "WysiwygRenderer.h"
#include "render/GlyphCache.h"
#include "render/FontSettings.h"
#include "editor/CharStyle.h"
#include "editor/TextBuffer.h"
#include "editor/FormattedTextBuffer.h"
#include "platform/AssetPath.h"

#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace
{
    // US Letter, hardcoded for Phase 1.
    constexpr double kPaperWidthIn  = 8.5;
    constexpr double kPaperHeightIn = 11.0;
    constexpr int    kPageGapPx     = 16; // empty space between stacked page rects

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

    // Resolve a CharFormat's face/size with fallback to document defaults.
    inline FontFace ResolveFace(const CharFormat& f, FontFace defaultFace) {
        if (f.face == CharFormat::Inherit) return defaultFace;
        if (f.face >= static_cast<uint8_t>(FontFace::Count_)) return defaultFace;
        return static_cast<FontFace>(f.face);
    }
    inline int ResolveSize(const CharFormat& f, int defaultPointSize) {
        if (f.size == CharFormat::Inherit) return defaultPointSize;
        if (f.size >= 4) return defaultPointSize; // FontSize enum has 4 values
        return FontSizePoints(FontSizeAt(static_cast<int>(f.size)));
    }
}

// ---------------------------------------------------------------------------
// Cache management
// ---------------------------------------------------------------------------

GlyphCache* WysiwygRenderer::CacheFor(FontFace face, int pointSize, int dpi)
{
    if (dpi != m_lastDpi)
    {
        // DPI change invalidates every cache — the pxSize used at
        // construction depends on dpi. Drop everything and rebuild lazily.
        m_caches.clear();
        m_lastDpi = dpi;
    }
    pointSize = std::max(1, pointSize);
    uint32_t key = MakeCacheKey(face, pointSize);
    auto it = m_caches.find(key);
    if (it != m_caches.end()) return it->second.get();

    // Pixel size at the renderer's effective dpi (typically 72 on screen).
    // Matches CreateFont's -MulDiv(point, dpi, 72) so the per-cell metrics
    // align with what the print path will produce.
    int pxSize = std::max(1, (pointSize * dpi + 36) / 72);
    auto cache = std::make_unique<GlyphCache>(m_sdl, face, pxSize);
    GlyphCache* raw = cache.get();
    m_caches[key] = std::move(cache);
    return raw;
}

// ---------------------------------------------------------------------------
// Wrap + layout helpers (pixel-based)
// ---------------------------------------------------------------------------

namespace
{
    // Per-character resolved render data — captured once per Draw call to
    // avoid repeatedly walking the CharFormat / cache lookup per glyph.
    struct CharRender
    {
        char        ch;
        int         advance;     // px
        int         lineHeight;  // px (LineHeight of the cache used)
        GlyphCache* cache;
        uint8_t     style;
    };

    // Per-segment metadata after pixel-based wrap. `startCol` is the
    // source column where this visual segment begins; `endCol` is exclusive.
    // `height` is max LineHeight among chars in the segment (or the empty-
    // line fallback when the segment has no chars).
    struct LineSegment
    {
        int startCol;
        int endCol;
        int height;
    };

    // Wrap by pixel-width, emitting segments whose summed advance fits
    // within `usableW`. Breaks at the most recent whitespace when possible
    // and consumes that whitespace (matches Print.cpp's behavior). Empty
    // source lines produce one zero-length segment so they still consume a
    // row.
    std::vector<LineSegment> WrapLinePixels(const std::vector<CharRender>& chars,
                                            int usableW, int emptyLineHeight)
    {
        std::vector<LineSegment> out;
        if (chars.empty())
        {
            out.push_back(LineSegment{ 0, 0, emptyLineHeight });
            return out;
        }
        if (usableW <= 0)
        {
            out.push_back(LineSegment{ 0, static_cast<int>(chars.size()),
                                       emptyLineHeight });
            return out;
        }

        int segStart   = 0;
        int xAccum     = 0;
        int lastBreak  = -1; // source col of last whitespace inside current seg
        int segHeight  = 0;

        auto emit = [&](int endExcl) {
            int h = segHeight > 0 ? segHeight : emptyLineHeight;
            out.push_back(LineSegment{ segStart, endExcl, h });
        };

        const int n = static_cast<int>(chars.size());
        for (int c = 0; c < n; ++c)
        {
            const CharRender& cr = chars[c];
            if (xAccum + cr.advance > usableW && c > segStart)
            {
                int breakAt = (lastBreak > segStart) ? lastBreak : c;
                emit(breakAt);
                // Skip the breaking whitespace if we broke on one.
                segStart = (breakAt == c) ? c : (breakAt + 1);
                xAccum   = 0;
                segHeight = 0;
                lastBreak = -1;
                // Restart accumulation from segStart including c.
                for (int k = segStart; k <= c; ++k)
                {
                    xAccum += chars[k].advance;
                    if (chars[k].lineHeight > segHeight) segHeight = chars[k].lineHeight;
                    if (chars[k].ch == ' ' || chars[k].ch == '\t') lastBreak = k;
                }
            }
            else
            {
                xAccum += cr.advance;
                if (cr.lineHeight > segHeight) segHeight = cr.lineHeight;
                if (cr.ch == ' ' || cr.ch == '\t') lastBreak = c;
            }
        }
        emit(n);
        return out;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

WysiwygRenderer::WysiwygRenderer(SDL_Renderer* sdl, const Theme& theme)
    : m_sdl(sdl), m_theme(theme)
{
}

WysiwygRenderer::~WysiwygRenderer() = default;

int WysiwygRenderer::ComputeCharsPerLine(FontFace face, int ptSize,
                                          double leftMarginIn, double rightMarginIn)
{
    double usableIn = kPaperWidthIn - leftMarginIn - rightMarginIn;
    if (usableIn <= 0.0) return 1;

    // Open the font at the actual rendering ptSize and read the integer
    // glyph advance. Matches what GlyphCache produces at draw time so the
    // chars-per-line budget agrees with the renderer's actual layout. This
    // is now used only by the Print path and cursor-navigation helper —
    // the screen renderer wraps pixel-by-pixel based on per-char advance.
    std::string path = ResolveAssetPath(FontFaceFile(face));
    TTF_Font* font = TTF_OpenFont(path.c_str(), static_cast<float>(ptSize));
    int advancePx = 0;
    if (font)
    {
        int minx, maxx, miny, maxy, adv;
        if (TTF_GetGlyphMetrics(font, U'M', &minx, &maxx, &miny, &maxy, &adv)
            && adv > 0)
        {
            advancePx = adv;
        }
        TTF_CloseFont(font);
    }
    if (advancePx <= 0)
        advancePx = std::max(1, ptSize / 2);

    double charIn = advancePx / 72.0;
    if (charIn <= 0.0) return 1;
    int cpl = static_cast<int>(usableIn / charIn);
    return std::max(1, cpl);
}

// ---------------------------------------------------------------------------
// Pre-pass: build the per-character render data for an entire document.
// Returns one vector<CharRender> per buffer line.
// ---------------------------------------------------------------------------

namespace
{
    // Captures the layout state shared between Draw and ClampScrollForCursor
    // so both produce the exact same pagination.
    struct LayoutPass
    {
        std::vector<std::vector<CharRender>>  chars;     // [line][col]
        std::vector<std::vector<LineSegment>> segments;  // [line][segIdx]
        int defaultLineHeight = 16;
    };
}

static void BuildLayoutPass(LayoutPass& out,
                            const TextBuffer& buf,
                            const FormattedTextBuffer* fmt,
                            FontFace defaultFace, int defaultPointSize,
                            int usableW,
                            std::function<GlyphCache*(FontFace, int)> cacheFor)
{
    int n = buf.LineCount();
    out.chars.clear();
    out.segments.clear();
    out.chars.resize(n);
    out.segments.resize(n);

    GlyphCache* defaultCache = cacheFor(defaultFace, defaultPointSize);
    out.defaultLineHeight = defaultCache ? defaultCache->LineHeight() : defaultPointSize;

    for (int li = 0; li < n; ++li)
    {
        const std::string& line = buf.Line(li);
        out.chars[li].reserve(line.size());
        for (size_t c = 0; c < line.size(); ++c)
        {
            CharFormat f = fmt ? fmt->FormatAt(li, static_cast<int>(c)) : CharFormat{};
            FontFace face = ResolveFace(f, defaultFace);
            int     ptSz = ResolveSize(f, defaultPointSize);
            GlyphCache* cache = cacheFor(face, ptSz);
            char32_t cp = static_cast<char32_t>(static_cast<unsigned char>(line[c]));
            CharRender cr;
            cr.ch          = line[c];
            cr.style       = f.style;
            cr.cache       = cache;
            cr.advance     = cache ? cache->GlyphAdvance(cp, f.style) : ptSz / 2;
            cr.lineHeight  = cache ? cache->LineHeight() : ptSz;
            out.chars[li].push_back(cr);
        }
        out.segments[li] = WrapLinePixels(out.chars[li], usableW, out.defaultLineHeight);
    }
}

// ---------------------------------------------------------------------------
// ClampScrollForCursor
// ---------------------------------------------------------------------------

int WysiwygRenderer::ClampScrollForCursor(const DrawContext& ctx)
{
    if (!ctx.buffer) return ctx.viewportTopPx;
    const int dpi = std::max(48, ctx.screenDpi);

    LayoutPass pass;
    const int pageW    = static_cast<int>(kPaperWidthIn  * dpi);
    const int pageH    = static_cast<int>(kPaperHeightIn * dpi);
    const int mTop     = static_cast<int>(ctx.margins.topIn    * dpi);
    const int mBottom  = static_cast<int>(ctx.margins.bottomIn * dpi);
    const int mLeft    = static_cast<int>(ctx.margins.leftIn   * dpi);
    const int mRight   = static_cast<int>(ctx.margins.rightIn  * dpi);
    const int usableW  = std::max(1, pageW - mLeft - mRight);
    const int usableH  = std::max(1, pageH - mTop  - mBottom);
    const int pageStride = pageH + kPageGapPx;

    BuildLayoutPass(pass, *ctx.buffer, ctx.formatted,
                    ctx.face, ctx.pointSize, usableW,
                    [&](FontFace f, int p) { return CacheFor(f, p, dpi); });

    int row = std::clamp(ctx.cursorRow, 0, std::max(0, ctx.buffer->LineCount() - 1));

    // Walk visual lines in document order, accumulating Y inside each page.
    int curPage    = 0;
    int yInPage    = 0;
    int cursorY    = 0;
    int cursorPage = 0;
    int cursorH    = pass.defaultLineHeight;
    bool found = false;

    for (int li = 0; li < ctx.buffer->LineCount() && !found; ++li)
    {
        const auto& segs = pass.segments[li];
        for (size_t s = 0; s < segs.size(); ++s)
        {
            int h = segs[s].height;
            if (yInPage + h > usableH)
            {
                ++curPage;
                yInPage = 0;
            }
            if (li == row)
            {
                bool isLast = (s + 1 == segs.size());
                if (ctx.cursorCol <= segs[s].endCol || isLast)
                {
                    cursorY    = yInPage;
                    cursorPage = curPage;
                    cursorH    = h;
                    found = true;
                    break;
                }
            }
            yInPage += h;
        }
    }

    int cursorAbsY = cursorPage * pageStride + mTop + cursorY;

    if (ctx.editorAreaPxH >= pageH)
        return cursorPage * pageStride;

    int viewport = ctx.viewportTopPx;
    if (cursorAbsY < viewport)
        viewport = cursorAbsY;
    if (cursorAbsY + cursorH > viewport + ctx.editorAreaPxH)
        viewport = cursorAbsY + cursorH - ctx.editorAreaPxH;
    if (viewport < 0) viewport = 0;
    return viewport;
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void WysiwygRenderer::Draw(const DrawContext& ctx)
{
    if (!m_sdl || !ctx.buffer) return;
    const int dpi    = std::max(48, ctx.screenDpi);
    const int pageW  = static_cast<int>(kPaperWidthIn  * dpi);
    const int pageH  = static_cast<int>(kPaperHeightIn * dpi);
    const int mTop   = static_cast<int>(ctx.margins.topIn    * dpi);
    const int mBottom = static_cast<int>(ctx.margins.bottomIn * dpi);
    const int mLeft  = static_cast<int>(ctx.margins.leftIn   * dpi);
    const int mRight = static_cast<int>(ctx.margins.rightIn  * dpi);
    const int usableW = std::max(1, pageW - mLeft - mRight);
    const int usableH = std::max(1, pageH - mTop  - mBottom);

    // Default cache must exist (for empty-line height + fallback advance).
    GlyphCache* defaultCache = CacheFor(ctx.face, ctx.pointSize, dpi);
    if (!defaultCache || !defaultCache->IsValid()) return;

    LayoutPass pass;
    BuildLayoutPass(pass, *ctx.buffer, ctx.formatted,
                    ctx.face, ctx.pointSize, usableW,
                    [&](FontFace f, int p) { return CacheFor(f, p, dpi); });

    int pageX = ctx.editorAreaPxX + (ctx.editorAreaPxW - pageW) / 2;
    if (pageX < ctx.editorAreaPxX) pageX = ctx.editorAreaPxX;
    const int viewport = ctx.viewportTopPx;
    const int pageStride = pageH + kPageGapPx;

    SDL_Rect oldClip{}, newClip{
        ctx.editorAreaPxX, ctx.editorAreaPxY,
        ctx.editorAreaPxW, ctx.editorAreaPxH
    };
    SDL_GetRenderClipRect(m_sdl, &oldClip);
    bool hadClip = (oldClip.w > 0 && oldClip.h > 0);
    SDL_SetRenderClipRect(m_sdl, &newClip);

    FillRect(m_sdl, ctx.editorAreaPxX, ctx.editorAreaPxY,
             ctx.editorAreaPxW, ctx.editorAreaPxH, m_theme.background);

    // Pass A: paint page rectangles + margin guides. Need to know total
    // pages first — walk segments greedily.
    int totalPages = 1;
    {
        int yInPage = 0;
        for (int li = 0; li < ctx.buffer->LineCount(); ++li)
        {
            for (const auto& seg : pass.segments[li])
            {
                if (yInPage + seg.height > usableH)
                {
                    ++totalPages;
                    yInPage = 0;
                }
                yInPage += seg.height;
            }
        }
    }

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

    // Selection range, normalized.
    int sr = 0, sc = 0, er = 0, ec = 0;
    if (ctx.selActive)
    {
        sr = ctx.selAnchorRow; sc = ctx.selAnchorCol;
        er = ctx.cursorRow;    ec = ctx.cursorCol;
        if (sr > er || (sr == er && sc > ec)) { std::swap(sr, er); std::swap(sc, ec); }
    }

    // Pass B: paint text segment-by-segment with greedy pagination.
    int curPage = 0;
    int yInPage = 0;
    for (int li = 0; li < ctx.buffer->LineCount(); ++li)
    {
        const auto& chars = pass.chars[li];
        const auto& segs  = pass.segments[li];
        for (size_t s = 0; s < segs.size(); ++s)
        {
            int h = segs[s].height;
            if (yInPage + h > usableH)
            {
                ++curPage;
                yInPage = 0;
            }
            int pageTopY = ctx.editorAreaPxY - viewport + curPage * pageStride;
            int textY    = pageTopY + mTop + yInPage;
            int usableX  = pageX + mLeft;

            // Cull lines fully outside the editor area.
            if (textY + h < ctx.editorAreaPxY)
            {
                yInPage += h;
                continue;
            }
            if (textY > ctx.editorAreaPxY + ctx.editorAreaPxH) goto done;

            int segLo = segs[s].startCol;
            int segHi = segs[s].endCol;

            // Selection highlight (under glyphs).
            if (ctx.selActive && li >= sr && li <= er)
            {
                int selLo = (li == sr) ? sc : 0;
                int selHi = (li == er) ? ec : static_cast<int>(chars.size());
                int hiLo = std::max(selLo, segLo);
                int hiHi = std::min(selHi, segHi);
                if (hiHi > hiLo)
                {
                    int xLo = usableX;
                    for (int c = segLo; c < hiLo; ++c) xLo += chars[c].advance;
                    int xHi = xLo;
                    for (int c = hiLo; c < hiHi; ++c) xHi += chars[c].advance;
                    FillRect(m_sdl, xLo, textY, std::max(2, xHi - xLo), h,
                             m_theme.reverseBackground);
                }
            }

            // Glyphs.
            int x = usableX;
            for (int c = segLo; c < segHi; ++c)
            {
                const CharRender& cr = chars[c];
                char32_t cp = static_cast<char32_t>(static_cast<unsigned char>(cr.ch));

                Color fg = m_theme.normalText;
                if (ctx.selActive)
                {
                    int colLo = (li == sr) ? sc : 0;
                    int colHi = (li == er) ? ec : static_cast<int>(chars.size());
                    if (li >= sr && li <= er && c >= colLo && c < colHi)
                        fg = m_theme.reverseForeground;
                }

                if (cp > U' ' && cr.cache)
                    cr.cache->DrawGlyphAt(cp, x, textY, fg, cr.style);
                x += cr.advance;
            }

            // Cursor.
            if (ctx.cursorVisible && li == ctx.cursorRow
                && ctx.cursorCol >= segLo && ctx.cursorCol <= segHi)
            {
                int cx = usableX;
                for (int c = segLo; c < ctx.cursorCol && c < segHi; ++c)
                    cx += chars[c].advance;
                FillRect(m_sdl, cx, textY, 2, h, m_theme.brightText);
            }

            yInPage += h;
        }
    }
done:

    if (hadClip)
        SDL_SetRenderClipRect(m_sdl, &oldClip);
    else
        SDL_SetRenderClipRect(m_sdl, nullptr);
}
