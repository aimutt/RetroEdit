#pragma once
#include "Layout.h"
#include "MenuDefs.h"
#include "render/ScreenBuffer.h"
#include "render/Theme.h"
#include "editor/TextBuffer.h"
#include <string>
#include <vector>

struct Cursor;

struct EditorUiState
{
    const TextBuffer* textBuffer    = nullptr;
    int               viewportTop   = 0;
    int               viewportLeft  = 0;
    std::string       filename;
    bool              dirty         = false;
    std::string       statusMessage = "Ready";

    // Modal dialog overlay (centered, retro-styled)
    bool        dialogActive        = false;
    bool        dialogIsConfirm     = false;  // false = text input, true = Y/N confirm
    std::string dialogTitle;                  // shown in the box top border
    std::string dialogPrompt;                 // input label or confirm line 1
    std::string dialogPrompt2;                // confirm line 2 (unused for input)
    std::string dialogInput;                  // current text input (input dialogs only)
    std::string dialogHint;                   // overrides default "[Y] Yes  [N] No" hint
    bool        dialogCursorVisible = false;  // blinking cursor inside input field

    // Selection (anchor; active end is Cursor)
    bool selActive    = false;
    int  selAnchorRow = 0;
    int  selAnchorCol = 0;

    // Menu state
    bool menuBarActive = false;   // menu bar is focused
    bool menuOpen      = false;   // a dropdown is showing
    int  activeMenu    = -1;      // which top-level menu is highlighted
    int  activeItem    = -1;      // which dropdown item is highlighted

    // Overlay dialogs
    bool showHelp  = false;
    bool showAbout = false;

    // Font picker dialog (flat preset list).
    // A "preset" is a (face, size) combo encoded as:
    //   preset = face_idx * FontSizeCount() + size_idx
    bool showFontDialog        = false;
    int  fontDialogPresetIdx   = 0;
    int  fontDialogScrollTop   = 0;
    int  fontDialogActivePreset = 0;

    // Theme picker (Options > Theme...)
    bool themeDialogActive   = false;
    int  themeDialogFocusIdx = 0;     // highlighted row
    int  themeDialogActiveIdx = 0;    // currently-applied theme

    // Editor options
    bool wordWrap = false;

    // Find dialog (input field + case-insensitive checkbox)
    bool findDialogActive          = false;
    bool findDialogCaseInsensitive = false;
    int  findDialogFocus           = 0;   // 0 = input field, 1 = checkbox

    // Word count
    int  wordCount             = 0;
    bool showWordCount         = false;
    bool wordCountDialogActive = false;

    // Spell check
    bool spellCheckEnabled   = false;
    bool highlightMisspelled = false;
    struct MisspelledSpan { int row; int col; int len; };
    std::vector<MisspelledSpan> misspelledSpans;

    // Print dialog
    bool        printDialogActive  = false;
    int         printPrinterIdx    = 0;
    std::vector<std::string> printerList;
    std::string printCopiesText;
    std::string printFromText;
    std::string printToText;
    std::string printMarginText[4]; // top, bottom, left, right
    bool        printAllPages      = true;
    int         printOrientation   = 0;  // 0=Portrait, 1=Landscape
    int         printFocusField    = 0;  // mirror PrintField as int

};

class RetroUi
{
public:
    RetroUi(const Theme& theme, const Layout& layout);

    void Draw(ScreenBuffer& buffer, const Cursor& cursor, const EditorUiState& state);

    // Mouse hit-testing. Coordinates are in screen-cell space (not pixels).
    // Returns the menu index at cellCol on the menu bar row, or -1.
    int HitTestMenuBar(int cellCol) const;

    // Returns the dropdown item index at (cellCol, cellRow) for menuIdx, or
    // -1 if the point is outside the dropdown rectangle. Item index counts
    // separators, so the caller must check whether the corresponding label
    // is empty before activating. The toggle bools mirror the live shortcut
    // text used by DrawDropdownMenu so the rect width is computed identically.
    int HitTestDropdownItem(int menuIdx, int cellCol, int cellRow,
                            int screenColumns,
                            bool wordWrap, bool showWordCount,
                            bool spellCheckEnabled, bool highlightMisspelled) const;

    // Dialog hit-testing.
    // Each dialog has its own geometry, so each gets a dedicated hit-tester
    // returning an enum describing what was clicked. Callers also use the
    // *DialogRect helpers to detect click-outside-the-dialog (= cancel).
    struct Rect { int x = 0, y = 0, w = 0, h = 0; bool Contains(int c, int r) const
                  { return c >= x && c < x + w && r >= y && r < y + h; } };

    Rect InputDialogRect    (int screenColumns) const;
    Rect FindDialogRect     (int screenColumns) const;
    Rect WordCountDialogRect(int screenColumns) const;
    Rect ConfirmDialogRect  (int screenColumns) const;
    Rect FontDialogRect     (int screenColumns) const;
    Rect HelpScreenRect     (int screenColumns) const;
    Rect AboutScreenRect    (int screenColumns) const;

    enum class InputHit { None, OkHint, CancelHint };
    InputHit HitTestInputDialog(int cellCol, int cellRow, int screenColumns) const;

    enum class FindHit { None, InputField, Checkbox, OkHint, CancelHint };
    FindHit HitTestFindDialog(int cellCol, int cellRow, int screenColumns) const;

    enum class WordCountHit { None, Checkbox, CloseHint };
    WordCountHit HitTestWordCountDialog(int cellCol, int cellRow, int screenColumns) const;

    // Generic vertical-scrollbar hit-test. Mirrors the geometry that
    // DrawScrollbar paints, so any dialog with a scrollbar can call this
    // and translate the result into its own action.
    //
    // grabOffsetInThumb is filled in when region == Thumb — store it at
    // mousedown and pass it back into ComputeScrollTopFromThumbDrag on
    // mouse motion to recompute scrollTop.
    struct ScrollbarHit {
        enum class Region { None, UpButton, DownButton, Thumb, TrackAbove, TrackBelow };
        Region region            = Region::None;
        int    grabOffsetInThumb = 0;
    };
    ScrollbarHit HitTestScrollbar(int cellCol, int cellRow,
                                  int scrollbarX, int scrollbarY, int height,
                                  int totalItems, int visibleItems, int scrollTop) const;
    int ComputeScrollTopFromThumbDrag(int cellRow,
                                      int scrollbarY, int height,
                                      int totalItems, int visibleItems,
                                      int grabOffsetInThumb) const;

    enum class FontHit { None, PresetRow,
                         ScrollUp, ScrollDown,
                         ScrollThumb, ScrollTrackAbove, ScrollTrackBelow,
                         ApplyHint, CancelHint };
    struct FontDialogClick {
        FontHit hit               = FontHit::None;
        int     index             = -1;
        int     grabOffsetInThumb = 0;  // valid when hit == ScrollThumb
    };
    FontDialogClick HitTestFontDialog(int cellCol, int cellRow, int screenColumns,
                                       int presetCount, int scrollTop) const;

    Rect ThemeDialogRect(int screenColumns, int themeCount) const;
    enum class ThemeHit { None, Row, OkHint, CancelHint };
    struct ThemeDialogClick { ThemeHit hit = ThemeHit::None; int index = -1; };
    ThemeDialogClick HitTestThemeDialog(int cellCol, int cellRow, int screenColumns,
                                         int themeCount) const;

    enum class ConfirmHit { None, Yes, No, Cancel };
    ConfirmHit HitTestConfirmDialog(int cellCol, int cellRow, int screenColumns,
                                     const std::string& hint) const;

    enum class PrintHit {
        None,
        PrinterPrev, PrinterNext,
        Copies,
        RangeAll, RangeCustom, RangeFrom, RangeTo,
        Portrait, Landscape,
        MarginTop, MarginBottom, MarginLeft, MarginRight,
        OkHint, CancelHint
    };
    PrintHit HitTestPrintDialog(int cellCol, int cellRow, int screenColumns) const;
    Rect     PrintDialogRect   (int screenColumns) const;

private:
    const Theme& m_theme;
    Layout       m_layout;

    void DrawMenuBar(ScreenBuffer& buffer, bool menuBarActive, int activeMenu);
    void DrawTitleBar(ScreenBuffer& buffer, const std::string& filename, bool dirty);
    void DrawEditorArea(ScreenBuffer& buffer, const TextBuffer& textBuffer,
                        int viewportTop, const Cursor& cursor, const EditorUiState& state);
    void DrawStatusBar(ScreenBuffer& buffer, const Cursor& cursor, const EditorUiState& state);
    void DrawSeparator(ScreenBuffer& buffer, int row);
    void DrawFunctionKeyBar(ScreenBuffer& buffer);

    // Overlay rendering
    void DrawDropdownMenu(ScreenBuffer& buffer, int menuIdx, int activeItem,
                          const EditorUiState& state);
    void DrawHelpScreen(ScreenBuffer& buffer);
    void DrawAboutScreen(ScreenBuffer& buffer);
    void DrawInputDialog(ScreenBuffer& buffer, const std::string& title,
                         const std::string& label, const std::string& input,
                         bool cursorVisible);
    void DrawConfirmDialog(ScreenBuffer& buffer, const std::string& title,
                           const std::string& line1, const std::string& line2,
                           const std::string& hint);
    void DrawFontDialog(ScreenBuffer& buffer, const EditorUiState& state);
    void DrawThemeDialog(ScreenBuffer& buffer, const EditorUiState& state);
    void DrawFindDialog(ScreenBuffer& buffer, const EditorUiState& state);
    void DrawWordCountDialog(ScreenBuffer& buffer, const EditorUiState& state);
    void DrawPrintDialog(ScreenBuffer& buffer, const EditorUiState& state);
    void DrawBox(ScreenBuffer& buffer, int x, int y, int w, int h, Color fg, Color bg);

    // Reusable vertical scrollbar — see RetroDocWriter's RetroUi.h for the
    // full contract. Mirrors the same body so the two products can diverge
    // visually if needed.
    void DrawScrollbar(ScreenBuffer& buffer, int x, int y, int height,
                       int totalItems, int visibleItems, int scrollTop);
};
