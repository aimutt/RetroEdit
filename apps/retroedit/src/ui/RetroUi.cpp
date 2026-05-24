#include "RetroUi.h"
#include "app/Cursor.h"
#include "editor/Selection.h"
#include "editor/WordWrap.h"
#include "render/FontSettings.h"
#include <SDL3/SDL.h>
#include <string>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

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
    {
        DrawFontDialog(buffer, state);
        CheckDialogBoundsRight(buffer, FontDialogRect(buffer.Columns()), "FontDialog");
    }

    if (state.themeDialogActive)
    {
        DrawThemeDialog(buffer, state);
        // ThemeDialog rect depends on theme count which we don't have here;
        // skip the check rather than guess.
    }

    if (state.wordCountDialogActive)
    {
        DrawWordCountDialog(buffer, state);
        CheckDialogBoundsRight(buffer, WordCountDialogRect(buffer.Columns()), "WordCountDialog");
    }

    if (state.printDialogActive)
    {
        DrawPrintDialog(buffer, state);
        CheckDialogBoundsRight(buffer, PrintDialogRect(buffer.Columns()), "PrintDialog");
    }

    if (state.dialogActive)
    {
        if (state.dialogIsConfirm)
        {
            DrawConfirmDialog(buffer, state.dialogTitle,
                              state.dialogPrompt, state.dialogPrompt2,
                              state.dialogHint);
            CheckDialogBoundsRight(buffer, ConfirmDialogRect(buffer.Columns()), "ConfirmDialog");
        }
        else if (state.findDialogActive)
        {
            DrawFindDialog(buffer, state);
            CheckDialogBoundsRight(buffer, FindDialogRect(buffer.Columns()), "FindDialog");
        }
        else
        {
            DrawInputDialog(buffer, state.dialogTitle,
                            state.dialogPrompt, state.dialogInput,
                            state.dialogCursorVisible);
            CheckDialogBoundsRight(buffer, InputDialogRect(buffer.Columns()), "InputDialog");
        }
    }
}

void RetroUi::CheckDialogBoundsRight(const ScreenBuffer& buffer,
                                      const Rect& rect, const char* dialogName) const
{
    int rightOutsideCol = rect.x + rect.w;
    if (rightOutsideCol < 0 || rightOutsideCol >= buffer.Columns())
        return;  // dialog flush with the screen edge — no overrun cell to check
    for (int row = rect.y; row < rect.y + rect.h; ++row)
    {
        if (row < 0 || row >= buffer.Rows()) continue;
        const ScreenCell& cell = buffer.At(rightOutsideCol, row);
        if (cell.character > U' ')
        {
            SDL_Log("Dialog '%s' overflowed right border at (col=%d, row=%d) char=U+%04X",
                    dialogName, rightOutsideCol, row,
                    static_cast<unsigned>(cell.character));
            return;  // one log per dialog per frame is enough to surface the bug
        }
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

void RetroUi::DrawEditorArea(ScreenBuffer& buffer, const TextBuffer& /*textBuffer*/,
                              int /*viewportTop*/, const Cursor& /*cursor*/,
                              const EditorUiState& /*state*/)
{
    // Editor text is no longer drawn into the chrome ScreenBuffer — it has
    // its own renderer (EditorRenderer) painting at the document font's
    // cell size, layered between the chrome paint and SDL_RenderPresent in
    // Application::Render. Here we just fill the editor region with blank
    // background cells. Dialog overlays that draw into this region of the
    // ScreenBuffer still appear on top, since Application::Render skips
    // the editor pass when a prompt mode is active.
    for (int row = m_layout.ROW_EDITOR_FIRST; row <= m_layout.ROW_EDITOR_LAST; ++row)
        for (int col = 0; col < buffer.Columns(); ++col)
            buffer.PutChar(col, row, U' ', m_theme.normalText, m_theme.background);
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

    std::string pos;
    if (state.showWordCount)
        pos = "Words: " + std::to_string(state.wordCount) + "  ";
    pos += "Ln " + std::to_string(cursor.row + 1)
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
// Reusable vertical scrollbar
// ---------------------------------------------------------------------------
//
// Layout when height >= 3:
//     row 0          : U'▲' up-arrow button
//     rows 1..h-2    : track (U'░') and thumb (U'█')
//     row h-1        : U'▼' down-arrow button
// When height < 3 the arrows are dropped and the full column is track + thumb.
//
// Thumb extents (size + top, measured in cell rows *relative to scrollbarY*)
// are computed in one place so DrawScrollbar, HitTestScrollbar, and
// ComputeScrollTopFromThumbDrag all stay in sync.

namespace
{
    struct ThumbExtents { int top; int size; };

    ThumbExtents ComputeThumbExtents(int height, int totalItems,
                                     int visibleItems, int scrollTop)
    {
        ThumbExtents e{ 0, 0 };
        if (height <= 0 || totalItems <= visibleItems) return e;

        const bool hasArrows = (height >= 3);
        const int  trackY    = hasArrows ? 1 : 0;
        const int  trackH    = hasArrows ? height - 2 : height;

        e.size = std::max(1, (trackH * visibleItems) / totalItems);
        int maxScroll = std::max(1, totalItems - visibleItems);
        int rel = std::clamp(
            ((trackH - e.size) * scrollTop) / maxScroll,
            0, trackH - e.size);
        e.top = trackY + rel;
        return e;
    }
}

void RetroUi::DrawScrollbar(ScreenBuffer& buffer, int x, int y, int height,
                            int totalItems, int visibleItems, int scrollTop)
{
    if (height <= 0) return;

    Color track = m_theme.dimText;
    Color thumb = m_theme.brightText;
    Color bg    = m_theme.background;

    const bool hasArrows = (height >= 3);
    const int  trackY0   = hasArrows ? 1 : 0;
    const int  trackY1   = hasArrows ? height - 2 : height - 1;

    for (int r = trackY0; r <= trackY1; ++r)
        buffer.PutChar(x, y + r, U'░', track, bg);

    if (hasArrows)
    {
        buffer.PutChar(x, y,              U'▲', track, bg);
        buffer.PutChar(x, y + height - 1, U'▼', track, bg);
    }

    if (totalItems <= visibleItems) return;

    ThumbExtents t = ComputeThumbExtents(height, totalItems, visibleItems, scrollTop);
    for (int r = 0; r < t.size; ++r)
        buffer.PutChar(x, y + t.top + r, U'█', thumb, bg);
}

RetroUi::ScrollbarHit RetroUi::HitTestScrollbar(int cellCol, int cellRow,
                                                 int scrollbarX, int scrollbarY, int height,
                                                 int totalItems, int visibleItems,
                                                 int scrollTop) const
{
    ScrollbarHit out;
    if (cellCol != scrollbarX || height <= 0) return out;
    int k = cellRow - scrollbarY;
    if (k < 0 || k >= height) return out;

    const bool hasArrows = (height >= 3);
    if (hasArrows && k == 0)
    {
        out.region = ScrollbarHit::Region::UpButton;
        return out;
    }
    if (hasArrows && k == height - 1)
    {
        out.region = ScrollbarHit::Region::DownButton;
        return out;
    }

    if (totalItems <= visibleItems)
        return out;  // track exists but no thumb; no useful drag target

    ThumbExtents t = ComputeThumbExtents(height, totalItems, visibleItems, scrollTop);
    if (k >= t.top && k < t.top + t.size)
    {
        out.region            = ScrollbarHit::Region::Thumb;
        out.grabOffsetInThumb = k - t.top;
        return out;
    }
    out.region = (k < t.top) ? ScrollbarHit::Region::TrackAbove
                             : ScrollbarHit::Region::TrackBelow;
    return out;
}

int RetroUi::ComputeScrollTopFromThumbDrag(int cellRow,
                                            int scrollbarY, int height,
                                            int totalItems, int visibleItems,
                                            int grabOffsetInThumb) const
{
    if (height <= 0 || totalItems <= visibleItems) return 0;

    const bool hasArrows = (height >= 3);
    const int  trackY    = hasArrows ? 1 : 0;
    const int  trackH    = hasArrows ? height - 2 : height;

    int thumbSize = std::max(1, (trackH * visibleItems) / totalItems);
    int travel    = trackH - thumbSize;          // 0..travel inclusive
    if (travel <= 0) return 0;

    int desiredThumbTop = (cellRow - scrollbarY) - trackY - grabOffsetInThumb;
    desiredThumbTop = std::clamp(desiredThumbTop, 0, travel);

    int maxScroll = std::max(1, totalItems - visibleItems);
    return std::clamp((desiredThumbTop * maxScroll + travel / 2) / travel,
                      0, maxScroll);
}

// ---------------------------------------------------------------------------
// Dropdown menu
// ---------------------------------------------------------------------------

namespace
{
    // Effective shortcut text for an item — accounts for the Options menu's
    // live On/Off toggles whose static `shortcut` field is empty. Used by both
    // the width calc and the render path so the column doesn't get overrun
    // when "Off" is wider than the (empty) static shortcut.
    std::string LiveShortcut(int menuIdx, int itemIdx, const MenuItemDef& item,
                             bool wordWrap, bool showWordCount,
                             bool spellCheckEnabled, bool highlightMisspelled)
    {
        // Options menu (idx 5) has the live On/Off toggles.
        // Options menu (menuIdx 5) after Theme... inserted at item 1:
        // 0=Font, 1=Theme, 2=WordWrap, 3=WordCount, 4=Spell, 5=Highlight.
        if (menuIdx == 5 && itemIdx == 2) return wordWrap            ? "On" : "Off";
        if (menuIdx == 5 && itemIdx == 3) return showWordCount       ? "On" : "Off";
        if (menuIdx == 5 && itemIdx == 4) return spellCheckEnabled   ? "On" : "Off";
        if (menuIdx == 5 && itemIdx == 5) return highlightMisspelled ? "On" : "Off";
        return item.shortcut;
    }

    struct DropdownRect { int startCol; int startRow; int outerWidth; int numItems; };

    // Single source of truth for dropdown geometry, called by both
    // DrawDropdownMenu and HitTestDropdownItem so the two cannot drift.
    DropdownRect ComputeDropdownRect(int menuIdx, int screenColumns,
                                     bool wordWrap, bool showWordCount,
                                     bool spellCheckEnabled, bool highlightMisspelled,
                                     const Layout& layout)
    {
        DropdownRect r{ 0, layout.ROW_SEP_TOP, 0, 0 };
        const auto& menus = GetMenuDefs();
        if (menuIdx < 0 || menuIdx >= static_cast<int>(menus.size())) return r;
        const MenuDef& menu = menus[menuIdx];

        int innerWidth = 16; // minimum
        for (int i = 0; i < static_cast<int>(menu.items.size()); ++i)
        {
            const auto& item = menu.items[i];
            if (item.label.empty()) continue;
            int w = static_cast<int>(item.label.size());
            std::string sc = LiveShortcut(menuIdx, i, item,
                                          wordWrap, showWordCount,
                                          spellCheckEnabled, highlightMisspelled);
            if (!sc.empty())
                w += static_cast<int>(sc.size()) + 2; // two-space gap
            innerWidth = std::max(innerWidth, w);
        }
        innerWidth += 2; // one space padding each side
        int outerWidth = innerWidth + 2; // left and right border chars

        int startCol = menu.barCol - 1; // slight left offset to align with highlight
        if (startCol + outerWidth > screenColumns)
            startCol = screenColumns - outerWidth;
        if (startCol < 0) startCol = 0;

        r.startCol   = startCol;
        r.outerWidth = outerWidth;
        r.numItems   = static_cast<int>(menu.items.size());
        return r;
    }
}

void RetroUi::DrawDropdownMenu(ScreenBuffer& buffer, int menuIdx, int activeItem,
                                const EditorUiState& state)
{
    const auto& menus = GetMenuDefs();
    if (menuIdx < 0 || menuIdx >= static_cast<int>(menus.size())) return;
    const MenuDef& menu = menus[menuIdx];

    DropdownRect rect = ComputeDropdownRect(
        menuIdx, buffer.Columns(),
        state.wordWrap, state.showWordCount,
        state.spellCheckEnabled, state.highlightMisspelled,
        m_layout);

    int startCol  = rect.startCol;
    int startRow  = rect.startRow;
    int outerWidth = rect.outerWidth;
    int numItems  = rect.numItems;

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
            std::string shortcut = LiveShortcut(menuIdx, i, item,
                                                state.wordWrap, state.showWordCount,
                                                state.spellCheckEnabled,
                                                state.highlightMisspelled);
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
// Mouse hit-testing
// ---------------------------------------------------------------------------

int RetroUi::HitTestMenuBar(int cellCol) const
{
    const auto& menus = GetMenuDefs();
    for (int i = 0; i < static_cast<int>(menus.size()); ++i)
    {
        const MenuDef& m = menus[i];
        int start = m.barCol;
        int end   = m.barCol + static_cast<int>(m.label.size()); // exclusive
        if (cellCol >= start && cellCol < end)
            return i;
    }
    return -1;
}

int RetroUi::HitTestDropdownItem(int menuIdx, int cellCol, int cellRow,
                                 int screenColumns,
                                 bool wordWrap, bool showWordCount,
                                 bool spellCheckEnabled, bool highlightMisspelled) const
{
    DropdownRect rect = ComputeDropdownRect(
        menuIdx, screenColumns,
        wordWrap, showWordCount,
        spellCheckEnabled, highlightMisspelled,
        m_layout);
    if (rect.outerWidth <= 0 || rect.numItems <= 0) return -1;

    // Item rows sit between the top border (rect.startRow) and the bottom
    // border (rect.startRow + 1 + rect.numItems). Each item is one row tall.
    int itemRow = cellRow - rect.startRow - 1;
    if (itemRow < 0 || itemRow >= rect.numItems) return -1;

    if (cellCol < rect.startCol || cellCol >= rect.startCol + rect.outerWidth)
        return -1;

    return itemRow;
}

// ---------------------------------------------------------------------------
// Dialog hit-testing
// ---------------------------------------------------------------------------

namespace
{
    // Centers a (w x h) rectangle within (screenColumns x screenRows), clamped
    // to non-negative origin — mirroring the math every Draw*Dialog uses.
    RetroUi::Rect CenteredRect(int screenColumns, int screenRows, int w, int h)
    {
        RetroUi::Rect r;
        r.x = std::max(0, (screenColumns - w) / 2);
        r.y = std::max(0, (screenRows     - h) / 2);
        r.w = w;
        r.h = h;
        return r;
    }

    // Returns the uppercase contents of the [bracketed] token whose click
    // region contains clickCol. Tokens span from their '[' to just before the
    // next '[' (or end of string), so the user can click the bracket text
    // OR the explanatory text immediately after it. Returns "" on miss.
    // hintStartCol is the screen column where the first char of `hint` is rendered.
    std::string TokenAt(const std::string& hint, int hintStartCol, int clickCol)
    {
        int rel = clickCol - hintStartCol;
        if (rel < 0 || rel >= static_cast<int>(hint.size())) return {};

        // Find every '[' in the hint.
        std::vector<size_t> opens;
        for (size_t i = 0; i < hint.size(); ++i)
            if (hint[i] == '[') opens.push_back(i);
        if (opens.empty()) return {};

        // Which token region contains `rel`?
        size_t which = static_cast<size_t>(-1);
        for (size_t i = 0; i < opens.size(); ++i)
        {
            size_t lo = opens[i];
            size_t hi = (i + 1 < opens.size()) ? opens[i + 1] : hint.size();
            if (static_cast<size_t>(rel) >= lo && static_cast<size_t>(rel) < hi)
            {
                which = i;
                break;
            }
        }
        if (which == static_cast<size_t>(-1)) return {};

        // Extract content between [ and ]
        size_t lo = opens[which];
        size_t close = hint.find(']', lo);
        if (close == std::string::npos) return {};
        std::string token = hint.substr(lo + 1, close - lo - 1);
        for (auto& c : token)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return token;
    }
}

RetroUi::Rect RetroUi::InputDialogRect(int screenColumns) const
{
    return CenteredRect(screenColumns, m_layout.SCREEN_ROWS, 56, 7);
}

RetroUi::Rect RetroUi::FindDialogRect(int screenColumns) const
{
    return CenteredRect(screenColumns, m_layout.SCREEN_ROWS, 60, 9);
}

RetroUi::Rect RetroUi::WordCountDialogRect(int screenColumns) const
{
    return CenteredRect(screenColumns, m_layout.SCREEN_ROWS, 56, 9);
}

RetroUi::Rect RetroUi::ConfirmDialogRect(int screenColumns) const
{
    return CenteredRect(screenColumns, m_layout.SCREEN_ROWS, 56, 7);
}

constexpr int kFontDialogVisibleRows = 12;

RetroUi::Rect RetroUi::FontDialogRect(int screenColumns) const
{
    // Outer width 48 fits the bottom hint string + padding + border.
    return CenteredRect(screenColumns, m_layout.SCREEN_ROWS,
                        48, kFontDialogVisibleRows + 4);
}

RetroUi::Rect RetroUi::ThemeDialogRect(int screenColumns, int themeCount) const
{
    // Width 36 cells: enough for "Green (retro)" and "White (office)" plus
    // a 2-cell padding each side. Height = title row + blank + items + blank + hint.
    constexpr int kInnerW = 34;
    int outerWidth  = kInnerW + 2;
    int outerHeight = themeCount + 5;
    return CenteredRect(screenColumns, m_layout.SCREEN_ROWS, outerWidth, outerHeight);
}

RetroUi::Rect RetroUi::HelpScreenRect(int screenColumns) const
{
    // Mirror DrawHelpScreen constants: 13 lines, col width 24, +4 chrome rows.
    constexpr int numLines = 13;
    constexpr int outerWidth  = 24 * 2 + 2;
    constexpr int outerHeight = numLines + 4;
    return CenteredRect(screenColumns, m_layout.SCREEN_ROWS, outerWidth, outerHeight);
}

RetroUi::Rect RetroUi::AboutScreenRect(int screenColumns) const
{
    // Mirror DrawAboutScreen constants: 12 lines + 2 chrome rows, innerWidth 38.
    constexpr int numLines    = 12;
    constexpr int outerWidth  = 38 + 2;
    constexpr int outerHeight = numLines + 2;
    return CenteredRect(screenColumns, m_layout.SCREEN_ROWS, outerWidth, outerHeight);
}

RetroUi::InputHit RetroUi::HitTestInputDialog(int cellCol, int cellRow, int screenColumns) const
{
    Rect r = InputDialogRect(screenColumns);
    if (!r.Contains(cellCol, cellRow)) return InputHit::None;

    // Hint sits at y + outerHeight - 2 (= y + 5), drawn at x + 2.
    if (cellRow == r.y + r.h - 2)
    {
        std::string token = TokenAt("[Enter] OK    [Esc] Cancel", r.x + 2, cellCol);
        if (token == "ENTER") return InputHit::OkHint;
        if (token == "ESC")   return InputHit::CancelHint;
    }
    return InputHit::None;
}

RetroUi::FindHit RetroUi::HitTestFindDialog(int cellCol, int cellRow, int screenColumns) const
{
    Rect r = FindDialogRect(screenColumns);
    if (!r.Contains(cellCol, cellRow)) return FindHit::None;

    // Input field at y + 3, columns x+2 .. x + (outerWidth - 4) + 1 (inclusive of brackets)
    if (cellRow == r.y + 3)
    {
        int inputX = r.x + 2;
        int inputW = r.w - 4;
        if (cellCol >= inputX && cellCol < inputX + inputW)
            return FindHit::InputField;
    }
    // Checkbox row at y + 5 — full row click toggles for forgiveness.
    if (cellRow == r.y + 5) return FindHit::Checkbox;

    // Hint at y + outerHeight - 2 (= y + 7).
    if (cellRow == r.y + r.h - 2)
    {
        std::string token = TokenAt(
            "[Enter] Find  [Tab] Switch  [Space] Toggle  [Esc] Cancel",
            r.x + 2, cellCol);
        if (token == "ENTER") return FindHit::OkHint;
        if (token == "ESC")   return FindHit::CancelHint;
        if (token == "SPACE" || token == "TAB") return FindHit::Checkbox;
    }
    return FindHit::None;
}

RetroUi::WordCountHit RetroUi::HitTestWordCountDialog(int cellCol, int cellRow, int screenColumns) const
{
    Rect r = WordCountDialogRect(screenColumns);
    if (!r.Contains(cellCol, cellRow)) return WordCountHit::None;

    // Checkbox at y + 5
    if (cellRow == r.y + 5) return WordCountHit::Checkbox;

    // Hint at y + outerHeight - 2 (= y + 7)
    if (cellRow == r.y + r.h - 2)
    {
        std::string token = TokenAt("[Space] Toggle  [Enter/Esc] Close",
                                    r.x + 2, cellCol);
        if (token == "SPACE") return WordCountHit::Checkbox;
        // [Enter/Esc] both close — match the literal bracket text.
        if (token == "ENTER/ESC") return WordCountHit::CloseHint;
    }
    return WordCountHit::None;
}

RetroUi::FontDialogClick RetroUi::HitTestFontDialog(int cellCol, int cellRow, int screenColumns,
                                                     int presetCount, int scrollTop) const
{
    FontDialogClick out;
    Rect r = FontDialogRect(screenColumns);
    if (!r.Contains(cellCol, cellRow)) return out;

    const int visibleRows = std::min(kFontDialogVisibleRows, presetCount);

    if (cellRow == r.y + r.h - 2)
    {
        std::string token = TokenAt(
            "[Up/Down] Move  [Enter] Apply  [Esc] Cancel",
            r.x + 2, cellCol);
        if (token == "ENTER") { out.hit = FontHit::ApplyHint;  return out; }
        if (token == "ESC")   { out.hit = FontHit::CancelHint; return out; }
        return out;
    }

    // Scrollbar column — delegate to the generic helper, then map regions
    // onto the dialog-specific FontHit values.
    if (cellCol == r.x + r.w - 2)
    {
        ScrollbarHit sb = HitTestScrollbar(cellCol, cellRow,
                                           r.x + r.w - 2, r.y + 1, visibleRows,
                                           presetCount, visibleRows, scrollTop);
        switch (sb.region)
        {
            case ScrollbarHit::Region::UpButton:    out.hit = FontHit::ScrollUp;        break;
            case ScrollbarHit::Region::DownButton:  out.hit = FontHit::ScrollDown;      break;
            case ScrollbarHit::Region::Thumb:
                out.hit               = FontHit::ScrollThumb;
                out.grabOffsetInThumb = sb.grabOffsetInThumb;
                break;
            case ScrollbarHit::Region::TrackAbove:  out.hit = FontHit::ScrollTrackAbove; break;
            case ScrollbarHit::Region::TrackBelow:  out.hit = FontHit::ScrollTrackBelow; break;
            case ScrollbarHit::Region::None:        break;
        }
        return out;
    }

    int k = cellRow - (r.y + 1);
    if (k >= 0 && k < visibleRows)
    {
        int presetIdx = scrollTop + k;
        if (presetIdx >= 0 && presetIdx < presetCount
            && cellCol >= r.x + 1 && cellCol < r.x + r.w - 2)
        {
            out.hit   = FontHit::PresetRow;
            out.index = presetIdx;
            return out;
        }
    }
    return out;
}

RetroUi::ConfirmHit RetroUi::HitTestConfirmDialog(int cellCol, int cellRow, int screenColumns,
                                                   const std::string& hint) const
{
    Rect r = ConfirmDialogRect(screenColumns);
    if (!r.Contains(cellCol, cellRow)) return ConfirmHit::None;

    if (cellRow == r.y + r.h - 2)
    {
        // Either the caller-provided hint or the default if empty.
        const std::string effective = hint.empty() ? "[Y] Yes      [N] No" : hint;
        std::string token = TokenAt(effective, r.x + 2, cellCol);
        if (token == "Y")   return ConfirmHit::Yes;
        if (token == "N")   return ConfirmHit::No;
        if (token == "ESC") return ConfirmHit::Cancel;
    }
    return ConfirmHit::None;
}

// ---------------------------------------------------------------------------
// Print dialog
// ---------------------------------------------------------------------------
//
// Layout (outerWidth = 56, outerHeight = 16, all coordinates relative to the
// dialog's (x, y)):
//
//   row 2  "Printer:"   '<' at col 14, name at col 16..49, '>' at col 51
//   row 4  "Copies:"    "[ NN ]" at cols 14..19
//   row 6  "Pages:"     "(*) All" at 14..20, "( ) From [N] To [N]" at 22..42
//   row 8  "Orient.:"   "(*) Portrait" at 14..25, "( ) Landscape" at 29..41
//   row 10 "Margins (in):"
//   row 11 "  Top:  [0.50]    Bottom: [0.50]"
//   row 12 "  Left: [0.75]    Right:  [0.75]"
//   row 14 hint
//
// Numeric-field column ranges (rendered with surrounding brackets):
//
//   Copies      cols 14..19  (6 cells: "[ NN ]")
//   Range From  cols 31..34  (4 cells: "[NN]")
//   Range To    cols 39..42  (4 cells: "[NN]")
//   Margin Top    row 11, cols 10..15  (6 cells: "[N.NN]")
//   Margin Bottom row 11, cols 27..32  (6 cells)
//   Margin Left   row 12, cols 10..15  (6 cells)
//   Margin Right  row 12, cols 27..32  (6 cells)

namespace
{
    constexpr int kPrintW = 56;
    constexpr int kPrintH = 16;

    // Column offsets (relative to dialog x).
    constexpr int kPrintLabelCol  = 2;
    constexpr int kPrintValueCol  = 14;
    constexpr int kPrintPrinterArrLeft  = 14;
    constexpr int kPrintPrinterArrRight = kPrintW - 5;  // col 51

    // Hint row content (rendered at y + 14)
    const char* const kPrintHint = "[Tab] Next  [Enter] Print  [Esc] Cancel";
}

RetroUi::Rect RetroUi::PrintDialogRect(int screenColumns) const
{
    return CenteredRect(screenColumns, m_layout.SCREEN_ROWS, kPrintW, kPrintH);
}

void RetroUi::DrawPrintDialog(ScreenBuffer& buffer, const EditorUiState& state)
{
    Rect r = PrintDialogRect(buffer.Columns());
    const int x = r.x;
    const int y = r.y;

    Color fg     = m_theme.normalText;
    Color bg     = m_theme.background;
    Color bright = m_theme.brightText;
    Color dim    = m_theme.dimText;

    DrawBox(buffer, x, y, r.w, r.h, fg, bg);

    // Title centred in top border
    std::string title = " Print ";
    int titleX = x + (r.w - static_cast<int>(title.size())) / 2;
    buffer.WriteText(titleX, y, title, bright, bg);

    auto isFocus = [&](int field) { return state.printFocusField == field; };
    auto focusColors = [&](bool focused, Color& outFg, Color& outBg) {
        outFg = focused ? m_theme.reverseForeground : bright;
        outBg = focused ? m_theme.reverseBackground : bg;
    };

    // Field index integers must match the PrintField enum in Application.h.
    enum {
        F_Printer = 0, F_Copies, F_RangeMode, F_RangeFrom, F_RangeTo,
        F_Orientation, F_MarginTop, F_MarginBottom, F_MarginLeft, F_MarginRight
    };

    auto drawBracketField = [&](int row, int col, int width,
                                 const std::string& text, bool focused) {
        Color tfg, tbg;
        focusColors(focused, tfg, tbg);
        buffer.PutChar(x + col,             y + row, U'[', dim, bg);
        buffer.PutChar(x + col + width - 1, y + row, U']', dim, bg);
        // Fill interior with bg color (focused or not)
        for (int c = 1; c < width - 1; ++c)
            buffer.PutChar(x + col + c, y + row, U' ', tfg, tbg);
        // Right-align text inside the brackets
        int inner    = width - 2;
        int textCols = std::min(static_cast<int>(text.size()), inner);
        int startC   = col + 1 + (inner - textCols);
        for (int i = 0; i < textCols; ++i)
            buffer.PutChar(x + startC + i, y + row,
                           static_cast<char32_t>(static_cast<unsigned char>(text[i])),
                           tfg, tbg);
    };

    // Row 2 — Printer
    buffer.WriteText(x + kPrintLabelCol, y + 2, "Printer:", fg, bg);
    {
        bool focused = isFocus(F_Printer);
        Color afg = focused ? m_theme.reverseForeground : dim;
        Color abg = focused ? m_theme.reverseBackground : bg;
        buffer.PutChar(x + kPrintPrinterArrLeft,  y + 2, U'<', afg, abg);
        buffer.PutChar(x + kPrintPrinterArrRight, y + 2, U'>', afg, abg);
        // Name slot between arrows, fill with bg if focused
        int slotStart = kPrintPrinterArrLeft + 2;
        int slotWidth = kPrintPrinterArrRight - 1 - slotStart;
        if (slotWidth > 0)
        {
            Color tfg, tbg;
            focusColors(focused, tfg, tbg);
            for (int c = 0; c < slotWidth; ++c)
                buffer.PutChar(x + slotStart + c, y + 2, U' ', tfg, tbg);
            std::string name;
            if (state.printPrinterIdx >= 0
                && state.printPrinterIdx < static_cast<int>(state.printerList.size()))
                name = state.printerList[state.printPrinterIdx];
            if (static_cast<int>(name.size()) > slotWidth) name.resize(slotWidth);
            buffer.WriteText(x + slotStart, y + 2, name, tfg, tbg);
        }
    }

    // Row 4 — Copies
    buffer.WriteText(x + kPrintLabelCol, y + 4, "Copies:", fg, bg);
    drawBracketField(4, kPrintValueCol, 6, state.printCopiesText, isFocus(F_Copies));

    // Row 6 — Pages range
    buffer.WriteText(x + kPrintLabelCol, y + 6, "Pages:", fg, bg);
    {
        // All radio
        bool focused = isFocus(F_RangeMode);
        Color rfg, rbg;
        focusColors(focused, rfg, rbg);
        const char* mark = state.printAllPages ? "(*)" : "( )";
        buffer.WriteText(x + 14, y + 6, mark, rfg, rbg);
        buffer.WriteText(x + 18, y + 6, "All", fg, bg);

        // From/To radio
        const char* mark2 = state.printAllPages ? "( )" : "(*)";
        buffer.WriteText(x + 22, y + 6, mark2, rfg, rbg);
        buffer.WriteText(x + 26, y + 6, "From", fg, bg);
        drawBracketField(6, 31, 4, state.printFromText, isFocus(F_RangeFrom));
        buffer.WriteText(x + 36, y + 6, "To", fg, bg);
        drawBracketField(6, 39, 4, state.printToText, isFocus(F_RangeTo));
    }

    // Row 8 — Orientation
    buffer.WriteText(x + kPrintLabelCol, y + 8, "Orientation:", fg, bg);
    {
        bool focused = isFocus(F_Orientation);
        Color rfg, rbg;
        focusColors(focused, rfg, rbg);
        const char* p = (state.printOrientation == 0) ? "(*)" : "( )";
        const char* l = (state.printOrientation == 0) ? "( )" : "(*)";
        buffer.WriteText(x + 14, y + 8, p, rfg, rbg);
        buffer.WriteText(x + 18, y + 8, "Portrait", fg, bg);
        buffer.WriteText(x + 29, y + 8, l, rfg, rbg);
        buffer.WriteText(x + 33, y + 8, "Landscape", fg, bg);
    }

    // Row 10 — Margins label
    buffer.WriteText(x + kPrintLabelCol, y + 10, "Margins (in):", fg, bg);

    // Row 11 — Top + Bottom
    buffer.WriteText(x + 4, y + 11, "Top:",    fg, bg);
    drawBracketField(11, 10, 6, state.printMarginText[0], isFocus(F_MarginTop));
    buffer.WriteText(x + 19, y + 11, "Bottom:", fg, bg);
    drawBracketField(11, 27, 6, state.printMarginText[1], isFocus(F_MarginBottom));

    // Row 12 — Left + Right
    buffer.WriteText(x + 4, y + 12, "Left:",   fg, bg);
    drawBracketField(12, 10, 6, state.printMarginText[2], isFocus(F_MarginLeft));
    buffer.WriteText(x + 19, y + 12, "Right:", fg, bg);
    drawBracketField(12, 27, 6, state.printMarginText[3], isFocus(F_MarginRight));

    // Row 14 — Hint
    buffer.WriteText(x + 2, y + 14, kPrintHint, dim, bg);
}

RetroUi::PrintHit RetroUi::HitTestPrintDialog(int cellCol, int cellRow, int screenColumns) const
{
    Rect r = PrintDialogRect(screenColumns);
    if (!r.Contains(cellCol, cellRow)) return PrintHit::None;

    const int rx = cellCol - r.x;
    const int ry = cellRow - r.y;

    auto in = [&](int col, int width) {
        return rx >= col && rx < col + width;
    };

    // Hint row
    if (ry == 14)
    {
        std::string tok = TokenAt(kPrintHint, r.x + 2, cellCol);
        if (tok == "ENTER") return PrintHit::OkHint;
        if (tok == "ESC")   return PrintHit::CancelHint;
        return PrintHit::None;
    }

    // Printer row
    if (ry == 2)
    {
        if (in(kPrintPrinterArrLeft, 1))  return PrintHit::PrinterPrev;
        if (in(kPrintPrinterArrRight, 1)) return PrintHit::PrinterNext;
        // Click on the printer name itself also cycles forward (convenience).
        if (in(kPrintPrinterArrLeft + 1,
               kPrintPrinterArrRight - kPrintPrinterArrLeft - 1))
            return PrintHit::PrinterNext;
        return PrintHit::None;
    }

    // Copies row
    if (ry == 4 && in(kPrintValueCol, 6)) return PrintHit::Copies;

    // Pages row
    if (ry == 6)
    {
        if (in(14, 3) || in(18, 3)) return PrintHit::RangeAll;     // "(*) All" / "( ) All"
        if (in(22, 3) || in(26, 4)) return PrintHit::RangeCustom;  // "( ) From"
        if (in(31, 4))              return PrintHit::RangeFrom;
        if (in(36, 2))              return PrintHit::RangeCustom;  // "To" label
        if (in(39, 4))              return PrintHit::RangeTo;
    }

    // Orientation row
    if (ry == 8)
    {
        if (in(14, 3) || in(18, 8)) return PrintHit::Portrait;
        if (in(29, 3) || in(33, 9)) return PrintHit::Landscape;
    }

    // Margin rows
    if (ry == 11)
    {
        if (in(10, 6)) return PrintHit::MarginTop;
        if (in(27, 6)) return PrintHit::MarginBottom;
    }
    if (ry == 12)
    {
        if (in(10, 6)) return PrintHit::MarginLeft;
        if (in(27, 6)) return PrintHit::MarginRight;
    }

    return PrintHit::None;
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
// Word count dialog (shows live count + status-bar toggle checkbox)
// ---------------------------------------------------------------------------

void RetroUi::DrawWordCountDialog(ScreenBuffer& buffer, const EditorUiState& state)
{
    // Inner usable text width = outerWidth - 4 (borders + padding).
    static const int outerWidth  = 56;
    static const int outerHeight = 9;

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
    std::string t = " Word Count ";
    int titleX = x + (outerWidth - static_cast<int>(t.size())) / 2;
    buffer.WriteText(titleX, y, t, bright, bg);

    // Count line
    std::string countLine = "Word count: " + std::to_string(state.wordCount);
    buffer.WriteText(x + 2, y + 2, countLine, bright, bg);

    // Checkbox row (always rendered with focus highlight — only control here)
    int   cbY        = y + 5;
    Color cbFg       = m_theme.reverseForeground;
    Color cbBg       = m_theme.reverseBackground;
    const char* mark = state.showWordCount ? "[X]" : "[ ]";
    std::string cbLabel = std::string(" ") + mark + " Show in status bar ";
    int cbWidth = static_cast<int>(cbLabel.size());
    for (int c = 0; c < cbWidth; ++c)
        buffer.PutChar(x + 2 + c, cbY, U' ', cbFg, cbBg);
    buffer.WriteText(x + 2, cbY, cbLabel, cbFg, cbBg);

    // Hint
    const char* hint = "[Space] Toggle  [Enter/Esc] Close";
    buffer.WriteText(x + 2, y + outerHeight - 2, hint, dim, bg);
}

// ---------------------------------------------------------------------------
// Font picker dialog
// ---------------------------------------------------------------------------

void RetroUi::DrawFontDialog(ScreenBuffer& buffer, const EditorUiState& state)
{
    // RetroEdit's cell-grid rendering requires monospace faces only — the
    // preset list iterates only the monospace portion of the FontFace enum.
    const int presetCount = FontFaceMonospaceCount() * FontSizeCount();
    const int visibleRows = std::min(kFontDialogVisibleRows, presetCount);
    Rect r = FontDialogRect(buffer.Columns());
    const int x = r.x;
    const int y = r.y;
    const int outerWidth  = r.w;
    const int outerHeight = r.h;
    // borders(2) + padding(2) + scrollbar column(1)
    const int listColW    = outerWidth - 5;

    Color fg     = m_theme.normalText;
    Color bg     = m_theme.background;
    Color bright = m_theme.brightText;
    Color dim    = m_theme.dimText;

    DrawBox(buffer, x, y, outerWidth, outerHeight, fg, bg);

    const char* title = " Select Font ";
    int titleX = x + (outerWidth - static_cast<int>(std::strlen(title))) / 2;
    buffer.WriteText(titleX, y, title, bright, bg);

    int scrollTop = std::clamp(state.fontDialogScrollTop, 0,
                               std::max(0, presetCount - visibleRows));

    for (int k = 0; k < visibleRows; ++k)
    {
        int presetIdx = scrollTop + k;
        if (presetIdx >= presetCount) break;

        int rowY     = y + 1 + k;
        bool focused = (presetIdx == state.fontDialogPresetIdx);
        bool active  = (presetIdx == state.fontDialogActivePreset);

        Color rowFg = focused ? m_theme.reverseForeground
                              : (active ? bright : fg);
        Color rowBg = focused ? m_theme.reverseBackground : bg;

        for (int c = 0; c < listColW; ++c)
            buffer.PutChar(x + 2 + c, rowY, U' ', rowFg, rowBg);

        FontFace face = static_cast<FontFace>(presetIdx / FontSizeCount());
        FontSize size = FontSizeAt(presetIdx % FontSizeCount());
        std::string label = " ";
        label += FontFaceName(face);
        label += " ";
        label += std::to_string(FontSizePoints(size));
        label += " pt";
        if (active) label += " *";
        if (static_cast<int>(label.size()) > listColW) label.resize(listColW);
        buffer.WriteText(x + 2, rowY, label, rowFg, rowBg);
    }

    // Vertical scrollbar in the rightmost interior column.
    DrawScrollbar(buffer, x + outerWidth - 2, y + 1, visibleRows,
                  presetCount, visibleRows, scrollTop);

    const char* hint = "[Up/Down] Move  [Enter] Apply  [Esc] Cancel";
    int hintX = x + 2;
    int hintY = y + outerHeight - 2;
    buffer.WriteText(hintX, hintY, hint, dim, bg);
}

// ---------------------------------------------------------------------------
// Theme picker dialog
// ---------------------------------------------------------------------------

void RetroUi::DrawThemeDialog(ScreenBuffer& buffer, const EditorUiState& state)
{
    const int themeCount = ThemeCount();
    Rect r = ThemeDialogRect(buffer.Columns(), themeCount);
    const int x = r.x, y = r.y;
    const int outerWidth  = r.w;
    const int outerHeight = r.h;

    Color fg     = m_theme.normalText;
    Color bg     = m_theme.background;
    Color bright = m_theme.brightText;
    Color dim    = m_theme.dimText;

    DrawBox(buffer, x, y, outerWidth, outerHeight, fg, bg);

    const char* title = " Select Theme ";
    int titleX = x + (outerWidth - static_cast<int>(std::strlen(title))) / 2;
    buffer.WriteText(titleX, y, title, bright, bg);

    const int innerW = outerWidth - 2;
    for (int i = 0; i < themeCount; ++i)
    {
        int rowY = y + 2 + i;
        bool focused = (i == state.themeDialogFocusIdx);
        Color rowFg = focused ? m_theme.reverseForeground
                              : (i == state.themeDialogActiveIdx ? bright : fg);
        Color rowBg = focused ? m_theme.reverseBackground : bg;

        for (int c = 0; c < innerW; ++c)
            buffer.PutChar(x + 1 + c, rowY, U' ', rowFg, rowBg);

        std::string label = " ";
        label += ThemeDisplayName(static_cast<ThemeName>(i));
        if (i == state.themeDialogActiveIdx) label += " *";
        if (static_cast<int>(label.size()) > innerW) label.resize(innerW);
        buffer.WriteText(x + 1, rowY, label, rowFg, rowBg);
    }

    const char* hint = "[Up/Down] Move  [Enter] Apply  [Esc] Cancel";
    int hintLen = static_cast<int>(std::strlen(hint));
    int hintX = x + (outerWidth - hintLen) / 2;
    if (hintX < x + 1) hintX = x + 1;
    buffer.WriteText(hintX, y + outerHeight - 2, hint, dim, bg);
}

RetroUi::ThemeDialogClick RetroUi::HitTestThemeDialog(int cellCol, int cellRow,
                                                      int screenColumns,
                                                      int themeCount) const
{
    Rect r = ThemeDialogRect(screenColumns, themeCount);
    ThemeDialogClick out;
    if (!r.Contains(cellCol, cellRow)) return out;

    int relRow = cellRow - r.y;
    // Row layout: 0 = top border, 1 = title gap, 2..2+themeCount-1 = items,
    // then blank row, then hint row, then bottom border.
    if (relRow >= 2 && relRow < 2 + themeCount)
    {
        out.hit   = ThemeHit::Row;
        out.index = relRow - 2;
        return out;
    }
    if (relRow == r.h - 2)
    {
        std::string tok = TokenAt("[Enter] Apply  [Esc] Cancel", r.x, cellCol);
        if (tok == "ENTER") out.hit = ThemeHit::OkHint;
        else if (tok == "ESC") out.hit = ThemeHit::CancelHint;
    }
    return out;
}
