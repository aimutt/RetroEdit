#pragma once
#include "FontFace.h"
#include "Theme.h"
#include <SDL3/SDL.h>
#include <memory>

class GlyphCache;
class TextBuffer;

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

    struct DrawContext
    {
        const TextBuffer* buffer = nullptr;
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
    };

    void Draw(const DrawContext& ctx);

    // Given the current cursor position, returns an updated viewportTopPx
    // value that keeps the cursor visible inside the editor area. Application
    // calls this before Draw so navigation past the bottom (or above the top)
    // of the editor area scrolls the page automatically.
    int ClampScrollForCursor(const DrawContext& ctx);

    // DPI-independent chars-per-line for a monospace font at `ptSize` with the
    // given page margins. Used by both the on-screen wrap and the print path
    // so the two layouts wrap at the same column count. Internally opens the
    // font at a high resolution and reads its em-advance ratio with sub-pixel
    // precision (avoiding the integer-rounding error that bit small screen
    // font sizes).
    static int ComputeCharsPerLine(FontFace face, int ptSize,
                                    double leftMarginIn, double rightMarginIn);

private:
    // Rebuild the internal GlyphCache if any of (face, pointSize, screenDpi)
    // differ from what we have cached. Called transparently from Draw().
    void EnsureFont(FontFace face, int pointSize, int dpi);

    SDL_Renderer* m_sdl    = nullptr;
    const Theme&  m_theme;
    std::unique_ptr<GlyphCache> m_glyphs;
    FontFace m_lastFace      = FontFace::CascadiaMono;
    int      m_lastPointSize = 0;
    int      m_lastDpi       = 0;
};
