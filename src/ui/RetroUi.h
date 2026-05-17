#pragma once
#include "Layout.h"
#include "MenuDefs.h"
#include "render/ScreenBuffer.h"
#include "render/Theme.h"
#include "editor/TextBuffer.h"
#include <string>

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

    // Font picker dialog (two-column: face | size)
    bool showFontDialog        = false;
    int  fontDialogFaceIdx     = 0;
    int  fontDialogSizeIdx     = 0;
    int  fontDialogFocusColumn = 0;   // 0 = face, 1 = size
    int  fontDialogActiveFace  = 0;   // currently-applied face index
    int  fontDialogActiveSize  = 0;   // currently-applied size index

    // Editor options
    bool wordWrap = false;

    // Find dialog (input field + case-insensitive checkbox)
    bool findDialogActive          = false;
    bool findDialogCaseInsensitive = false;
    int  findDialogFocus           = 0;   // 0 = input field, 1 = checkbox
};

class RetroUi
{
public:
    RetroUi(const Theme& theme, const Layout& layout);

    void Draw(ScreenBuffer& buffer, const Cursor& cursor, const EditorUiState& state);

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
    void DrawFindDialog(ScreenBuffer& buffer, const EditorUiState& state);
    void DrawBox(ScreenBuffer& buffer, int x, int y, int w, int h, Color fg, Color bg);
};
