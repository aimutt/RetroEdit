#pragma once
#include "render/FontFace.h"
#include "render/Theme.h"
#include <SDL3/SDL.h>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

class GlyphCache;
class TextBuffer;
class FormattedTextBuffer;

// Per-document page margins (in inches). Default = 1" all around.
struct WysiwygMargins
{
    double topIn    = 1.0;
    double bottomIn = 1.0;
    double leftIn   = 1.0;
    double rightIn  = 1.0;
};

// Proportional text renderer used when WYSIWYG mode is on. Draws a centered
// US Letter page rectangle inside the editor-area pixel region, then renders
// the text buffer's content inside the page margins using actual glyph
// advances (true pixel layout — matches what the printer will produce).
//
// SDL-aware but does not touch the cell-grid ScreenBuffer. Constructed once
// per Application; SetGlyphCache is called whenever the active font changes.
class WysiwygRenderer
{
public:
    WysiwygRenderer(SDL_Renderer* sdl, const Theme& theme);
    ~WysiwygRenderer();

    struct MisspelledSpan { int row; int col; int len; };

    struct DrawContext
    {
        const TextBuffer* buffer = nullptr;
        // Optional per-character formatting. When non-null, each character is
        // rendered with the corresponding CharStyle bitmask. When null, every
        // character is treated as styleBits = 0 (plain). Must point to the
        // same content as `buffer` (FormattedTextBuffer wraps a TextBuffer).
        const FormattedTextBuffer* formatted = nullptr;
        int  cursorRow     = 0;
        int  cursorCol     = 0;
        bool cursorVisible = false;
        bool selActive     = false;
        int  selAnchorRow  = 0;
        int  selAnchorCol  = 0;
        WysiwygMargins margins;
        // Pixel rect of the editor area on the screen.
        int  editorAreaPxX = 0;
        int  editorAreaPxY = 0;
        int  editorAreaPxW = 0;
        int  editorAreaPxH = 0;
        int  screenDpi     = 96;       // pixels per inch; supplied by Application
        int  viewportTopPx = 0;        // vertical scroll within the document
        // Document font — WYSIWYG draws at this font scaled to the screen DPI
        // so the on-screen size matches the printed page (Print.cpp uses the
        // same point size, scaled to the printer's DPI).
        FontFace face       = FontFace::CascadiaMono;
        int      pointSize  = 16;

        // "Insert" font — the face/size of the next character that would be
        // typed at the cursor. When the user picks a font from the Font
        // dialog without a selection, the document default is left alone
        // but next-typed chars get this face/size; the cursor on an empty
        // line should preview that height instead of the default's, so
        // moving from a 24pt paragraph to a 12pt one shrinks the cursor.
        FontFace insertFace      = FontFace::CascadiaMono;
        int      insertPointSize = 16;

        // Misspelled spans (highlightMisspelled gate already applied by
        // caller). Each entry is a contiguous run of misspelled chars on a
        // single buffer row. BuildLayoutPass overrides cr.color =
        // m_theme.misspelledText for chars whose (row, col) falls in any
        // span; the per-char selection path in Draw still wins at the
        // glyph-paint stage so selected text doesn't inherit the misspell
        // tint.
        std::vector<MisspelledSpan> misspelledSpans;
    };

    void Draw(const DrawContext& ctx);

    // Given the current cursor position, returns an updated viewportTopPx
    // value that keeps the cursor visible inside the editor area. Application
    // calls this before Draw so navigation past the bottom (or above the top)
    // of the editor area scrolls the page automatically.
    int ClampScrollForCursor(const DrawContext& ctx);

    // One visual line of the laid-out document. Application uses these to
    // navigate Up/Down arrows in mixed-size paragraphs — finding the
    // closest source column whose pixel position matches the cursor's.
    struct VisualLine
    {
        int bufferRow;            // source row in the TextBuffer
        int startCol;             // source col (inclusive) where seg begins
        int endCol;               // source col (exclusive) where seg ends
        // Pixel x of each column relative to the segment's left edge.
        // Size = (endCol - startCol + 1) — the trailing entry is the x
        // just past the last glyph, so the cursor can sit at end-of-segment.
        std::vector<int> charXs;
    };

    // Build the full visual layout for the given document context. Used by
    // cursor-arrow navigation so Up/Down lands at the right visual column
    // even when the previous/next visual row uses a different font size.
    std::vector<VisualLine> ComputeVisualLayout(const DrawContext& ctx);

    // DPI-independent chars-per-line for a monospace font at `ptSize` with the
    // given page margins. Used by both the on-screen wrap and the print path
    // so the two layouts wrap at the same column count. Internally opens the
    // font at a high resolution and reads its em-advance ratio with sub-pixel
    // precision (avoiding the integer-rounding error that bit small screen
    // font sizes).
    static int ComputeCharsPerLine(FontFace face, int ptSize,
                                    double leftMarginIn, double rightMarginIn);

private:
    // Resolve and return the cache for a (face, pointSize) combo at the
    // current dpi, creating it on demand. Caches are kept in m_caches
    // until the dpi changes (e.g. resize across monitors with different
    // DPIs) — then the whole map is invalidated.
    GlyphCache* CacheFor(FontFace face, int pointSize, int dpi);
    static uint32_t MakeCacheKey(FontFace face, int pointSize)
    {
        return (static_cast<uint32_t>(face) << 16)
             | static_cast<uint32_t>(pointSize & 0xFFFF);
    }

    // For wrap math we need sub-pixel-precision per-char advance — the
    // integer values that GlyphCache::GlyphAdvance returns are good for
    // rendering but truncate at small ptSize, making the screen wrap
    // disagree with LibreOffice/print which use higher precision. Open
    // the font once per FontFace at a high reference size and read the
    // integer advance there; the sub-pixel advance at any ptSize is
    // hires_advance / kRefSize * ptSize.
    struct HiResEntry { void* font = nullptr; };  // opaque TTF_Font*
    std::unordered_map<int, HiResEntry> m_hiRes;

    // Returns the sub-pixel advance (in screen pixels at dpi=72, since
    // the editor uses an effective dpi of 72) for the given char at the
    // given face/ptSize. Falls back to ptSize / 2.0 when the font can't
    // be opened or the glyph has no metric.
    double SubpxAdvance(FontFace face, int pointSize, unsigned int codepoint);

    SDL_Renderer* m_sdl    = nullptr;
    const Theme&  m_theme;
    std::unordered_map<uint32_t, std::unique_ptr<GlyphCache>> m_caches;
    int      m_lastDpi  = 0;
};
