#pragma once
#include "render/FontSettings.h"
#include "render/Theme.h"
#include <SDL3/SDL.h>
#include <memory>
#include <vector>

class GlyphCache;
class TextBuffer;
struct Cursor;

// Monospace cell-grid renderer for the EDITOR body in RetroEdit.
//
// RetroEdit historically drew its editor text into the same ScreenBuffer
// that the menu bar, status bar, and dialogs use, sharing one GlyphCache
// at one cell size. Splitting the document font from the chrome font
// (Options > Font now targets only typed text) means the editor needs its
// own glyph cache and its own cell metrics — independent of the chrome's.
//
// EditorRenderer holds its own GlyphCache built from the document
// FontSettings (DPI-scaled to true point size, matching RetroRenderer
// and WysiwygRenderer's pt→px conversion). It paints visible TextBuffer
// rows directly to SDL inside the editor's pixel rectangle, on top of the
// chrome layer that RetroRenderer has already painted. The chrome's
// ScreenBuffer cells in the editor region are left as plain background
// spaces — only this renderer draws editor text and the editor cursor.
class EditorRenderer
{
public:
    EditorRenderer(SDL_Renderer* sdl, const Theme& theme, const FontSettings& settings);
    ~EditorRenderer();

    void SetFontSettings(const FontSettings& settings);

    // Editor cell metrics (in pixels). Used by Application to compute the
    // number of editor columns/rows that fit in the editor pixel rect, to
    // translate mouse pixel positions back to (row, col), and to size the
    // scroll viewport.
    int CellWidth()  const;
    int CellHeight() const;
    bool IsValid()   const;

    struct MisspelledSpan { int row; int col; int len; };

    struct DrawContext
    {
        const TextBuffer* buffer = nullptr;

        // Editor pixel rect inside the SDL window (computed by Application
        // from the chrome layout). Cells outside this rect are not touched.
        int editorPxX = 0;
        int editorPxY = 0;
        int editorPxW = 0;
        int editorPxH = 0;

        // Viewport — first visible buffer row and first visible buffer column.
        int viewportTop  = 0;
        int viewportLeft = 0;

        // Cursor.
        int  cursorRow     = 0;
        int  cursorCol     = 0;
        bool cursorVisible = false;

        // Selection (anchor + active end via cursor row/col).
        bool selActive    = false;
        int  selAnchorRow = 0;
        int  selAnchorCol = 0;

        // Display-only soft wrap. When true, the renderer breaks long buffer
        // lines into multiple visual rows on whitespace boundaries; viewportLeft
        // is ignored.
        bool wordWrap = false;

        // Misspelled spans (highlightMisspelled gate already applied by caller).
        std::vector<MisspelledSpan> misspelledSpans;
    };

    void Draw(const DrawContext& ctx);

private:
    SDL_Renderer*               m_sdl;
    const Theme&                m_theme;
    FontSettings                m_settings;
    std::unique_ptr<GlyphCache> m_glyphs;

    void RebuildGlyphCache();
};
