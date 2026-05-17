#include "RetroUi.h"
#include "app/Cursor.h"
#include "editor/Selection.h"
#include "editor/WordWrap.h"
#include "render/FontSettings.h"
#include <string>
#include <algorithm>
#include <cstring>

RetroUi::RetroUi(const Theme& theme, const Layout& layout)
    : m_theme(theme), m_layout(layout)
{
}

void RetroUi::Draw(ScreenBuffer& buffer, const Cursor& cursor, const EditorUiState& state)
{
    DrawMenuBar(buffer, state.menuBarActive, state.activeMenu);
    DrawTitleBar(buffer, state.filename, state.dirty);
    if (state.textBuffer)
        DrawEditorArea(buffer, *state.textBuffer, state.viewportTop, cursor, state);
    DrawStatusBar(buffer, cursor, state);
    DrawSeparator(buffer, m_layout.ROW_SEP_BOT);
    DrawFunctionKeyBar(buffer);

    // Overlays drawn last so they appear on top
    if (state.menuOpen)
        DrawDropdownMenu(buffer, state.activeMenu, state.activeItem, state);

    if (state.showHelp)
        DrawHelpScreen(buffer);

    if (state.showAbout)
        DrawAboutScreen(buffer);

    if (state.showFontDialog)
        DrawFontDialog(buffer, state);

    if (state.dialogActive)
    {
        if (state.dialogIsConfirm)
            DrawConfirmDialog(buffer, state.dialogTitle,
                              state.dialogPrompt, state.dialogPrompt2,
                              state.dialogHint);
        else if (state.findDialogActive)
            DrawFindDialog(buffer, state);
        else
            DrawInputDialog(buffer, state.dialogTitle,
                            state.dialogPrompt, state.dialogInput,
                            state.dialogCursorVisible);
    }
}

// ---------------------------------------------------------------------------
// Menu bar
// ---------------------------------------------------------------------------

void RetroUi::DrawMenuBar(ScreenBuffer& buffer, bool menuBarActive, int activeMenu)
{
    // Fill entire bar with reverse-video background
    for (int col = 0; col < buffer.Columns(); ++col)
        buffer.PutChar(col, m_layout.ROW_MENUBAR, U' ',
                       m_theme.reverseForeground, m_theme.reverseBackground);

    // Draw each menu title
    const auto& menus = GetMenuDefs();
    for (int i = 0; i < static_cast<int>(menus.size()); ++i)
    {
        const MenuDef& menu = menus[i];
        bool isActive = menuBarActive && (i == activeMenu);

        // Active title: bright text on dark background (stands out from bar)
        Color fg = isActive ? m_theme.brightText    : m_theme.reverseForeground;
        Color bg = isActive ? m_theme.background    : m_theme.reverseBackground;

        // Write one padding space before and after the label when active
        if (isActive)
        {
            buffer.PutChar(menu.barCol - 1, m_layout.ROW_MENUBAR, U' ', fg, bg);
            buffer.WriteText(menu.barCol, m_layout.ROW_MENUBAR, menu.label, fg, bg);
            int endCol = menu.barCol + static_cast<int>(menu.label.size());
            buffer.PutChar(endCol, m_layout.ROW_MENUBAR, U' ', fg, bg);
        }
        else
        {
            buffer.WriteText(menu.barCol, m_layout.ROW_MENUBAR, menu.label, fg, bg);
        }
    }
}

// ---------------------------------------------------------------------------
// Title bar (separator with filename)
// ---------------------------------------------------------------------------

void RetroUi::DrawTitleBar(ScreenBuffer& buffer, const std::string& filename, bool dirty)
{
    for (int col = 0; col < buffer.Columns(); ++col)
        buffer.PutChar(col, m_layout.ROW_SEP_TOP, U'-', m_theme.border, m_theme.background);

    std::string label = " " + (filename.empty() ? "untitled" : filename);
    if (dirty) label += " *";
    label += " ";

    int startCol = (buffer.Columns() - static_cast<int>(label.size())) / 2;
    startCol = std::max(0, startCol);
    buffer.WriteText(startCol, m_layout.ROW_SEP_TOP, label,
                     m_theme.brightText, m_theme.background);
}

// ---------------------------------------------------------------------------
// Editor area
// ---------------------------------------------------------------------------

void RetroUi::DrawEditorArea(ScreenBuffer& buffer, const TextBuffer& textBuffer,
                              int viewportTop, const Cursor& cursor,
                              const EditorUiState& state)
{
    Selection sel;
    sel.active    = state.selActive;
    sel.anchorRow = state.selAnchorRow;
    sel.anchorCol = state.selAnchorCol;

    if (!state.wordWrap)
    {
        // Single buffer row per screen row, with horizontal scroll.
        for (int screenRow = m_layout.ROW_EDITOR_FIRST;
             screenRow <= m_layout.ROW_EDITOR_LAST; ++screenRow)
        {
            int bufLine = viewportTop + (screenRow - m_layout.ROW_EDITOR_FIRST);
            if (bufLine < 0 || bufLine >= textBuffer.LineCount())
                continue;

            const std::string& lineStr = textBuffer.Line(bufLine);
            int lineLen = static_cast<int>(lineStr.size());

            for (int screenCol = 0; screenCol < buffer.Columns(); ++screenCol)
            {
                int bufCol = screenCol + state.viewportLeft;

                char32_t ch = (bufCol >= 0 && bufCol < lineLen)
                              ? static_cast<char32_t>(static_cast<unsigned char>(lineStr[bufCol]))
                              : U' ';

                bool selected = sel.ContainsCell(bufCol, bufLine,
                                                 cursor.row, cursor.column);

                Color fg = selected ? m_theme.reverseForeground : m_theme.normalText;
                Color bg = selected ? m_theme.reverseBackground : m_theme.background;

                buffer.PutChar(screenCol, screenRow, ch, fg, bg);
            }
        }
        return;
    }

    // Word-wrap: each buffer row may span several display rows; segments
    // break on the last whitespace that fits, falling back to a hard cut for
    // a word longer than the screen width.
    const int width      = buffer.Columns();
    int       screenRow  = m_layout.ROW_EDITOR_FIRST;
    int       bufLine    = viewportTop;

    while (screenRow <= m_layout.ROW_EDITOR_LAST && bufLine < textBuffer.LineCount())
    {
        const std::string& lineStr = textBuffer.Line(bufLine);
        const int lineLen          = static_cast<int>(lineStr.size());
        std::vector<int> starts    = ComputeWrapStarts(lineStr, width);

        for (int seg = 0;
             seg < static_cast<int>(starts.size()) && screenRow <= m_layout.ROW_EDITOR_LAST;
             ++seg, ++screenRow)
        {
            int segStart = starts[seg];
            int segEnd   = (seg + 1 < static_cast<int>(starts.size()))
                           ? starts[seg + 1]
                           : lineLen;

            for (int screenCol = 0; screenCol < width; ++screenCol)
            {
                int  bufCol    = segStart + screenCol;
                bool inSegment = (bufCol < segEnd);

                char32_t ch = inSegment
                              ? static_cast<char32_t>(static_cast<unsigned char>(lineStr[bufCol]))
                              : U' ';

                bool selected = inSegment
                                && sel.ContainsCell(bufCol, bufLine,
                                                    cursor.row, cursor.column);

                Color fg = selected ? m_theme.reverseForeground : m_theme.normalText;
                Color bg = selected ? m_theme.reverseBackground : m_theme.background;

                buffer.PutChar(screenCol, screenRow, ch, fg, bg);
            }
        }
        ++bufLine;
    }
}

// ---------------------------------------------------------------------------
// Status bar
// ---------------------------------------------------------------------------

void RetroUi::DrawStatusBar(ScreenBuffer& buffer, const Cursor& cursor,
                             const EditorUiState& state)
{
    for (int col = 0; col < buffer.Columns(); ++col)
        buffer.PutChar(col, m_layout.ROW_STATUS, U' ',
                       m_theme.normalText, m_theme.background);

    buffer.WriteText(1, m_layout.ROW_STATUS, state.statusMessage,
                     m_theme.normalText, m_theme.background);

    std::string pos = "Ln " + std::to_string(cursor.row + 1)
                    + ", Col " + std::to_string(cursor.column + 1);
    int posX = buffer.Columns() - static_cast<int>(pos.size()) - 1;
    if (posX > 0)
        buffer.WriteText(posX, m_layout.ROW_STATUS, pos,
                         m_theme.dimText, m_theme.background);
}

// ---------------------------------------------------------------------------
// Separator and function key bar
// ---------------------------------------------------------------------------

void RetroUi::DrawSeparator(ScreenBuffer& buffer, int row)
{
    for (int col = 0; col < buffer.Columns(); ++col)
        buffer.PutChar(col, row, U'-', m_theme.border, m_theme.background);
}

void RetroUi::DrawFunctionKeyBar(ScreenBuffer& buffer)
{
    for (int col = 0; col < buffer.Columns(); ++col)
        buffer.PutChar(col, m_layout.ROW_FKEYS, U' ',
                       m_theme.reverseForeground, m_theme.reverseBackground);

    buffer.WriteText(0, m_layout.ROW_FKEYS,
                     " F1 Help  F2 Save  F3 Open  F6 Find  ^Z Undo  F10 Menu  Esc Exit",
                     m_theme.reverseForeground, m_theme.reverseBackground);
}

// ---------------------------------------------------------------------------
// DrawBox helper
// ---------------------------------------------------------------------------

void RetroUi::DrawBox(ScreenBuffer& buffer, int x, int y, int w, int h,
                      Color fg, Color bg)
{
    if (w < 2 || h < 2) return;

    // Top border
    buffer.PutChar(x, y, U'+', fg, bg);
    for (int c = 1; c < w - 1; ++c)
        buffer.PutChar(x + c, y, U'-', fg, bg);
    buffer.PutChar(x + w - 1, y, U'+', fg, bg);

    // Sides with filled interior
    for (int r = 1; r < h - 1; ++r)
    {
        buffer.PutChar(x, y + r, U'|', fg, bg);
        for (int c = 1; c < w - 1; ++c)
            buffer.PutChar(x + c, y + r, U' ', fg, bg);
        buffer.PutChar(x + w - 1, y + r, U'|', fg, bg);
    }

    // Bottom border
    buffer.PutChar(x, y + h - 1, U'+', fg, bg);
    for (int c = 1; c < w - 1; ++c)
        buffer.PutChar(x + c, y + h - 1, U'-', fg, bg);
    buffer.PutChar(x + w - 1, y + h - 1, U'+', fg, bg);
}

// ---------------------------------------------------------------------------
// Dropdown menu
// ---------------------------------------------------------------------------

void RetroUi::DrawDropdownMenu(ScreenBuffer& buffer, int menuIdx, int activeItem,
                                const EditorUiState& state)
{
    const auto& menus = GetMenuDefs();
    if (menuIdx < 0 || menuIdx >= static_cast<int>(menus.size())) return;
    const MenuDef& menu = menus[menuIdx];

    // Compute required inner width: label + gap + shortcut
    int innerWidth = 16; // minimum
    for (const auto& item : menu.items)
    {
        if (item.label.empty()) continue;
        int w = static_cast<int>(item.label.size());
        if (!item.shortcut.empty())
            w += static_cast<int>(item.shortcut.size()) + 2; // two-space gap
        innerWidth = std::max(innerWidth, w);
    }
    innerWidth += 2; // one space padding each side
    int outerWidth = innerWidth + 2; // left and right border chars

    // Position: open below the menu bar title, clamped to screen
    int startCol = menu.barCol - 1; // slight left offset to align with highlight
    if (startCol + outerWidth > buffer.Columns())
        startCol = buffer.Columns() - outerWidth;
    if (startCol < 0) startCol = 0;

    int startRow  = m_layout.ROW_SEP_TOP; // row 1, just below menu bar
    int numItems  = static_cast<int>(menu.items.size());

    Color fg = m_theme.normalText;
    Color bg = m_theme.background;

    // Top border
    buffer.PutChar(startCol, startRow, U'+', fg, bg);
    for (int c = 1; c < outerWidth - 1; ++c)
        buffer.PutChar(startCol + c, startRow, U'-', fg, bg);
    buffer.PutChar(startCol + outerWidth - 1, startRow, U'+', fg, bg);

    // Item rows
    for (int i = 0; i < numItems; ++i)
    {
        int rowY            = startRow + 1 + i;
        const auto& item    = menu.items[i];
        bool isSeparator    = item.label.empty();
        bool isHighlighted  = (!isSeparator && i == activeItem);

        Color itemFg = isHighlighted ? m_theme.reverseForeground : m_theme.normalText;
        Color itemBg = isHighlighted ? m_theme.reverseBackground : m_theme.background;

        if (isSeparator)
        {
            // Separator: full-width horizontal divider
            buffer.PutChar(startCol, rowY, U'+', fg, bg);
            for (int c = 1; c < outerWidth - 1; ++c)
                buffer.PutChar(startCol + c, rowY, U'-', fg, bg);
            buffer.PutChar(startCol + outerWidth - 1, rowY, U'+', fg, bg);
        }
        else
        {
            // Left border
            buffer.PutChar(startCol, rowY, U'|', fg, bg);

            // Fill row background
            for (int c = 1; c < outerWidth - 1; ++c)
                buffer.PutChar(startCol + c, rowY, U' ', itemFg, itemBg);

            // Right border
            buffer.PutChar(startCol + outerWidth - 1, rowY, U'|', fg, bg);

            // Label (left-aligned with one space padding)
            buffer.WriteText(startCol + 1, rowY, " " + item.label, itemFg, itemBg);

            // Shortcut (right-aligned with one space from right border).
            // Options > Word Wrap shows live On/Off state.
            std::string shortcut = item.shortcut;
            if (menuIdx == 6 && i == 1)
                shortcut = state.wordWrap ? "On" : "Off";
            if (!shortcut.empty())
            {
                Color scFg = isHighlighted ? m_theme.reverseForeground : m_theme.dimText;
                int   scX  = startCol + outerWidth - 1
                             - static_cast<int>(shortcut.size()) - 1;
                buffer.WriteText(scX, rowY, shortcut, scFg, itemBg);
            }
        }
    }

    // Bottom border
    int bottomRow = startRow + 1 + numItems;
    buffer.PutChar(startCol, bottomRow, U'+', fg, bg);
    for (int c = 1; c < outerWidth - 1; ++c)
        buffer.PutChar(startCol + c, bottomRow, U'-', fg, bg);
    buffer.PutChar(startCol + outerWidth - 1, bottomRow, U'+', fg, bg);
}

// ---------------------------------------------------------------------------
// Help screen overlay
// ---------------------------------------------------------------------------

void RetroUi::DrawHelpScreen(ScreenBuffer& buffer)
{
    // Lines are laid out as two columns: left (function keys) and right (Ctrl shortcuts)
    struct HelpLine { const char* left; const char* right; };
    static const HelpLine lines[] = {
        { " F1   Help",           " Ctrl+N  New File"      },
        { " F2   Save",           " Ctrl+O  Open"          },
        { " F3   Open",           " Ctrl+S  Save"          },
        { " F6   Find Next",      " Ctrl+Z  Undo"          },
        { " F10  Menu",           " Ctrl+Y  Redo"          },
        { " Esc  Exit/Cancel",    " Ctrl+F  Find"          },
        { "",                     " Ctrl+A  Select All"    },
        { " Arrows   Move",       " Ctrl+C  Copy"          },
        { " Home/End  Line",      " Ctrl+X  Cut"           },
        { " PgUp/Dn  Scroll",     " Ctrl+V  Paste"         },
        { "",                     ""                       },
        { " Alt+F  File Menu",    " Alt+E  Edit Menu"      },
        { " Alt+S  Search Menu",  " Alt+H  Help Menu"      },
    };

    static const int numLines    = static_cast<int>(sizeof(lines) / sizeof(lines[0]));
    static const int colWidth    = 24;  // width of each column
    static const int innerWidth  = colWidth * 2;
    static const int outerWidth  = innerWidth + 2;
    static const int outerHeight = numLines + 4; // title row + blank + lines + blank + close

    int x = (buffer.Columns() - outerWidth) / 2;
    int y = (m_layout.SCREEN_ROWS - outerHeight) / 2;
    x = std::max(0, x);
    y = std::max(0, y);

    Color fg     = m_theme.normalText;
    Color bg     = m_theme.background;
    Color bright = m_theme.brightText;
    Color dim    = m_theme.dimText;

    DrawBox(buffer, x, y, outerWidth, outerHeight, fg, bg);

    // Title centred in top border
    const char* title = " RetroEdit Help ";
    int titleLen = static_cast<int>(std::strlen(title));
    int titleX   = x + (outerWidth - titleLen) / 2;
    buffer.WriteText(titleX, y, title, bright, bg);

    // Content lines
    for (int i = 0; i < numLines; ++i)
    {
        int rowY = y + 2 + i;
        if (rowY >= y + outerHeight - 2) break;

        // Left column
        std::string left = lines[i].left;
        left.resize(colWidth, ' ');
        buffer.WriteText(x + 1, rowY, left, fg, bg);

        // Right column
        std::string right = lines[i].right;
        right.resize(colWidth, ' ');
        buffer.WriteText(x + 1 + colWidth, rowY, right, fg, bg);
    }

    // "Press any key" at second-to-last inner row
    const char* closeMsg = "Press any key to close";
    int closeMsgLen = static_cast<int>(std::strlen(closeMsg));
    int closeX      = x + (outerWidth - closeMsgLen) / 2;
    int closeY      = y + outerHeight - 2;
    buffer.WriteText(closeX, closeY, closeMsg, dim, bg);
}

// ---------------------------------------------------------------------------
// About screen overlay
// ---------------------------------------------------------------------------

void RetroUi::DrawAboutScreen(ScreenBuffer& buffer)
{
    static const char* aboutLines[] = {
        "",
        "          RetroEdit",
        "       Version 0.1 - Stage 4",
        "",
        "  A retro-style text editor inspired",
        "  by green monochrome CRT terminals",
        "  of the early 1980s.",
        "",
        "  Built with C++20 and SDL3.",
        "",
        "   Press any key to close",
        "",
    };

    static const int numLines    = static_cast<int>(sizeof(aboutLines) / sizeof(aboutLines[0]));
    static const int innerWidth  = 38;
    static const int outerWidth  = innerWidth + 2;
    static const int outerHeight = numLines + 2; // lines + top/bottom borders

    int x = (buffer.Columns() - outerWidth) / 2;
    int y = (m_layout.SCREEN_ROWS - outerHeight) / 2;
    x = std::max(0, x);
    y = std::max(0, y);

    Color fg     = m_theme.normalText;
    Color bg     = m_theme.background;
    Color bright = m_theme.brightText;
    Color dim    = m_theme.dimText;

    DrawBox(buffer, x, y, outerWidth, outerHeight, fg, bg);

    // Title in top border
    const char* title = " About RetroEdit ";
    int titleLen = static_cast<int>(std::strlen(title));
    int titleX   = x + (outerWidth - titleLen) / 2;
    buffer.WriteText(titleX, y, title, bright, bg);

    for (int i = 0; i < numLines; ++i)
    {
        int rowY = y + 1 + i;
        if (rowY >= y + outerHeight - 1) break;

        std::string line = aboutLines[i];
        // Pad to inner width so box background fills correctly
        if (static_cast<int>(line.size()) < innerWidth)
            line.resize(innerWidth, ' ');

        Color lineFg = (i == numLines - 2) ? dim : fg; // close-message hint in dim
        buffer.WriteText(x + 1, rowY, line, lineFg, bg);
    }
}

// ---------------------------------------------------------------------------
// Modal dialogs (centered text-mode windows for prompts and confirmations)
// ---------------------------------------------------------------------------

void RetroUi::DrawInputDialog(ScreenBuffer& buffer, const std::string& title,
                               const std::string& label, const std::string& input,
                               bool cursorVisible)
{
    static const int outerWidth  = 56;
    static const int outerHeight = 7;

    int x = (buffer.Columns() - outerWidth) / 2;
    int y = (m_layout.SCREEN_ROWS - outerHeight) / 2;
    x = std::max(0, x);
    y = std::max(0, y);

    Color fg     = m_theme.normalText;
    Color bg     = m_theme.background;
    Color bright = m_theme.brightText;
    Color dim    = m_theme.dimText;

    DrawBox(buffer, x, y, outerWidth, outerHeight, fg, bg);

    // Title centred in top border
    std::string t = " " + title + " ";
    int titleX = x + (outerWidth - static_cast<int>(t.size())) / 2;
    buffer.WriteText(titleX, y, t, bright, bg);

    // Prompt label one line below the title
    buffer.WriteText(x + 2, y + 2, label, fg, bg);

    // Bracketed input field
    int inputX = x + 2;
    int inputY = y + 3;
    int inputW = outerWidth - 4;          // total width including brackets
    int textW  = inputW - 2;              // characters available between brackets

    buffer.PutChar(inputX, inputY, U'[', dim, bg);
    buffer.PutChar(inputX + inputW - 1, inputY, U']', dim, bg);

    // Reserve one cell for the cursor; clip from the left if the text is too long
    int visibleMax = textW - 1;
    if (visibleMax < 0) visibleMax = 0;
    std::string display = input;
    if (static_cast<int>(display.size()) > visibleMax)
        display = display.substr(display.size() - visibleMax);

    for (int i = 0; i < static_cast<int>(display.size()); ++i)
        buffer.PutChar(inputX + 1 + i,
                       inputY,
                       static_cast<char32_t>(static_cast<unsigned char>(display[i])),
                       bright, bg);

    // Block cursor (uses the existing blink cadence)
    int cursorCol = inputX + 1 + static_cast<int>(display.size());
    if (cursorVisible
        && cursorCol >= inputX + 1
        && cursorCol <= inputX + inputW - 2)
    {
        buffer.At(cursorCol, inputY).reverseVideo = true;
    }

    // Hint at the bottom inner row
    const char* hint = "[Enter] OK    [Esc] Cancel";
    buffer.WriteText(x + 2, y + outerHeight - 2, hint, dim, bg);
}

void RetroUi::DrawConfirmDialog(ScreenBuffer& buffer, const std::string& title,
                                 const std::string& line1, const std::string& line2,
                                 const std::string& hint)
{
    static const int outerWidth  = 56;
    static const int outerHeight = 7;

    int x = (buffer.Columns() - outerWidth) / 2;
    int y = (m_layout.SCREEN_ROWS - outerHeight) / 2;
    x = std::max(0, x);
    y = std::max(0, y);

    Color fg     = m_theme.normalText;
    Color bg     = m_theme.background;
    Color bright = m_theme.brightText;
    Color dim    = m_theme.dimText;

    DrawBox(buffer, x, y, outerWidth, outerHeight, fg, bg);

    std::string t = " " + title + " ";
    int titleX = x + (outerWidth - static_cast<int>(t.size())) / 2;
    buffer.WriteText(titleX, y, t, bright, bg);

    if (!line1.empty())
        buffer.WriteText(x + 2, y + 2, line1, fg, bg);
    if (!line2.empty())
        buffer.WriteText(x + 2, y + 3, line2, fg, bg);

    std::string hintStr = hint.empty() ? "[Y] Yes      [N] No" : hint;
    buffer.WriteText(x + 2, y + outerHeight - 2, hintStr, dim, bg);
}

// ---------------------------------------------------------------------------
// Find dialog (input field + case-insensitive checkbox)
// ---------------------------------------------------------------------------

void RetroUi::DrawFindDialog(ScreenBuffer& buffer, const EditorUiState& state)
{
    // Width is sized to fit the bottom hint exactly: usable inner text width
    // is outerWidth - 4 (two borders + two padding cells), so a 56-char hint
    // needs outerWidth >= 60.
    static const int outerWidth  = 60;
    static const int outerHeight = 9;   // taller than a plain input dialog to fit the checkbox row

    int x = (buffer.Columns() - outerWidth) / 2;
    int y = (m_layout.SCREEN_ROWS - outerHeight) / 2;
    x = std::max(0, x);
    y = std::max(0, y);

    Color fg     = m_theme.normalText;
    Color bg     = m_theme.background;
    Color bright = m_theme.brightText;
    Color dim    = m_theme.dimText;

    DrawBox(buffer, x, y, outerWidth, outerHeight, fg, bg);

    // Title centred in top border
    std::string t = " Find ";
    int titleX = x + (outerWidth - static_cast<int>(t.size())) / 2;
    buffer.WriteText(titleX, y, t, bright, bg);

    // Prompt label
    buffer.WriteText(x + 2, y + 2, "Search for:", fg, bg);

    // Bracketed input field
    const bool inputFocused = (state.findDialogFocus == 0);
    int inputX = x + 2;
    int inputY = y + 3;
    int inputW = outerWidth - 4;
    int textW  = inputW - 2;

    buffer.PutChar(inputX, inputY, U'[', dim, bg);
    buffer.PutChar(inputX + inputW - 1, inputY, U']', dim, bg);

    int visibleMax = textW - 1;
    if (visibleMax < 0) visibleMax = 0;
    std::string display = state.dialogInput;
    if (static_cast<int>(display.size()) > visibleMax)
        display = display.substr(display.size() - visibleMax);

    for (int i = 0; i < static_cast<int>(display.size()); ++i)
        buffer.PutChar(inputX + 1 + i,
                       inputY,
                       static_cast<char32_t>(static_cast<unsigned char>(display[i])),
                       bright, bg);

    // Block cursor (only when the input field has focus)
    int cursorCol = inputX + 1 + static_cast<int>(display.size());
    if (inputFocused
        && state.dialogCursorVisible
        && cursorCol >= inputX + 1
        && cursorCol <= inputX + inputW - 2)
    {
        buffer.At(cursorCol, inputY).reverseVideo = true;
    }

    // Checkbox row
    int   cbY        = y + 5;
    bool  cbFocused  = (state.findDialogFocus == 1);
    Color cbFg       = cbFocused ? m_theme.reverseForeground : fg;
    Color cbBg       = cbFocused ? m_theme.reverseBackground : bg;
    const char* mark = state.findDialogCaseInsensitive ? "[X]" : "[ ]";
    std::string cbLabel = std::string(" ") + mark + " Case insensitive ";
    // Pad highlight to a fixed width so focus is visible
    int cbWidth = static_cast<int>(cbLabel.size());
    for (int c = 0; c < cbWidth; ++c)
        buffer.PutChar(x + 2 + c, cbY, U' ', cbFg, cbBg);
    buffer.WriteText(x + 2, cbY, cbLabel, cbFg, cbBg);

    // Hint at the bottom inner row
    const char* hint = "[Enter] Find  [Tab] Switch  [Space] Toggle  [Esc] Cancel";
    buffer.WriteText(x + 2, y + outerHeight - 2, hint, dim, bg);
}

// ---------------------------------------------------------------------------
// Font picker dialog
// ---------------------------------------------------------------------------

void RetroUi::DrawFontDialog(ScreenBuffer& buffer, const EditorUiState& state)
{
    const int faceCount  = FontFaceCount();
    const int sizeCount  = FontSizeCount();
    const int listRows   = std::max(faceCount, sizeCount);

    // Sized so the bottom hint string fits inside the right border.
    const int outerWidth = 60;
    const int faceColW   = 28;
    const int sizeColW   = outerWidth - 5 - faceColW; // borders(2) + padding(2) + divider(1)
    const int outerHeight = listRows + 5; // top, header, items..., blank, hint, bottom

    int x = (buffer.Columns() - outerWidth) / 2;
    int y = (m_layout.SCREEN_ROWS - outerHeight) / 2;
    x = std::max(0, x);
    y = std::max(0, y);

    Color fg     = m_theme.normalText;
    Color bg     = m_theme.background;
    Color bright = m_theme.brightText;
    Color dim    = m_theme.dimText;

    DrawBox(buffer, x, y, outerWidth, outerHeight, fg, bg);

    // Title centred in top border
    const char* title = " Select Font ";
    int titleX = x + (outerWidth - static_cast<int>(std::strlen(title))) / 2;
    buffer.WriteText(titleX, y, title, bright, bg);

    // Column headers on the first content row
    int headerY = y + 1;
    buffer.WriteText(x + 2,                    headerY, "Face", dim, bg);
    buffer.WriteText(x + 2 + faceColW + 1,     headerY, "Size", dim, bg);

    // Vertical divider between the two columns runs through the list rows
    int dividerX = x + 1 + faceColW + 1;
    for (int r = 0; r < listRows + 1; ++r)
        buffer.PutChar(dividerX, y + 1 + r, U'|', m_theme.border, bg);

    const bool faceFocused = (state.fontDialogFocusColumn == 0);

    // Face column
    for (int i = 0; i < faceCount; ++i)
    {
        int rowY        = y + 2 + i;
        bool highlight  = (i == state.fontDialogFaceIdx);
        bool isFocusHi  = highlight && faceFocused;

        Color rowFg = isFocusHi ? m_theme.reverseForeground
                                : (highlight ? bright : fg);
        Color rowBg = isFocusHi ? m_theme.reverseBackground : bg;

        for (int c = 0; c < faceColW; ++c)
            buffer.PutChar(x + 2 + c, rowY, U' ', rowFg, rowBg);

        std::string label = " ";
        label += FontFaceName(static_cast<FontFace>(i));
        if (i == state.fontDialogActiveFace) label += " *";
        if (static_cast<int>(label.size()) > faceColW) label.resize(faceColW);
        buffer.WriteText(x + 2, rowY, label, rowFg, rowBg);
    }

    // Size column
    for (int i = 0; i < sizeCount; ++i)
    {
        int rowY        = y + 2 + i;
        bool highlight  = (i == state.fontDialogSizeIdx);
        bool isFocusHi  = highlight && !faceFocused;

        Color rowFg = isFocusHi ? m_theme.reverseForeground
                                : (highlight ? bright : fg);
        Color rowBg = isFocusHi ? m_theme.reverseBackground : bg;

        for (int c = 0; c < sizeColW; ++c)
            buffer.PutChar(x + 2 + faceColW + 1 + c, rowY, U' ', rowFg, rowBg);

        std::string label = " ";
        label += FontSizeName(FontSizeAt(i));
        if (i == state.fontDialogActiveSize) label += " *";
        if (static_cast<int>(label.size()) > sizeColW) label.resize(sizeColW);
        buffer.WriteText(x + 2 + faceColW + 1, rowY, label, rowFg, rowBg);
    }

    // Hint at the bottom inner row
    const char* hint = "[Up/Down] Move  [Tab] Switch  [Enter] Apply  [Esc] Cancel";
    int hintX = x + 2;
    int hintY = y + outerHeight - 2;
    buffer.WriteText(hintX, hintY, hint, dim, bg);

    // Footer note explaining the asterisk marker
    if (outerHeight - 2 - 1 > 2 + listRows)
    {
        // there's a blank line between the lists and the hint
    }
}
