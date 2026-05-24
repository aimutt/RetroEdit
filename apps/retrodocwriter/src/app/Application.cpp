#include "Application.h"
#include "editor/CharStyle.h"
#include "editor/Palette.h"
#include "editor/WordCount.h"
#include "platform/AppData.h"
#include "platform/AssetPath.h"
#include "platform/Beep.h"
#include <algorithm>
#include <cctype>

// Window starts at this size. Both dimensions track live drag-resizes after
// startup; the screen-buffer column/row count is derived from current window
// pixels divided by the active font's cell pixel size.
static constexpr int    DEFAULT_WINDOW_WIDTH  = 900;
static constexpr int    DEFAULT_WINDOW_HEIGHT = 960;
static constexpr int    MIN_WINDOW_WIDTH      = 480;
static constexpr int    MIN_WINDOW_HEIGHT     = 320;
static constexpr int    MIN_SCREEN_ROWS       = 12;
static constexpr int    MIN_SCREEN_COLUMNS    = 30;
static constexpr Uint64 BLINK_INTERVAL_MS     = 500;

int Application::ComputeScreenRows(int cellHeight) const
{
    if (cellHeight <= 0) return MIN_SCREEN_ROWS;
    return std::max(m_windowHeight / cellHeight, MIN_SCREEN_ROWS);
}

int Application::ComputeScreenColumns(int cellWidth) const
{
    if (cellWidth <= 0) return MIN_SCREEN_COLUMNS;
    return std::max(m_windowWidth / cellWidth, MIN_SCREEN_COLUMNS);
}

Application::Application()
{
    m_sdlInitialized = SDL_Init(SDL_INIT_VIDEO);
    if (!m_sdlInitialized)
    {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return;
    }

    // Default font: Cascadia Mono Medium — clean, professional, legible.
    m_fontSettings = FontSettings{ FontFace::CascadiaMono, FontSize::Medium };

    m_windowWidth  = DEFAULT_WINDOW_WIDTH;
    m_windowHeight = DEFAULT_WINDOW_HEIGHT;

    m_window = std::make_unique<Window>("RetroDocWriter", m_windowWidth, m_windowHeight);
    if (!m_window->IsValid())
        return;
    SDL_SetWindowMinimumSize(m_window->GetWindow(), MIN_WINDOW_WIDTH, MIN_WINDOW_HEIGHT);

    // Pick up the actual window size in case the OS clamped or scaled it.
    SDL_GetWindowSize(m_window->GetWindow(), &m_windowWidth, &m_windowHeight);

    m_renderer = std::make_unique<RetroRenderer>(m_window->GetRenderer(), m_fontSettings);
    m_wysiwyg  = std::make_unique<WysiwygRenderer>(m_window->GetRenderer(), m_theme);

    m_screenColumns = ComputeScreenColumns(m_renderer->CellWidth());
    int rows        = ComputeScreenRows(m_renderer->CellHeight());
    m_layout        = Layout(rows);

    m_screenBuffer = std::make_unique<ScreenBuffer>(m_screenColumns, rows);
    m_ui           = std::make_unique<RetroUi>(m_theme, m_layout);
    m_document     = std::make_unique<RichFileDocument>();

    SDL_StartTextInput(m_window->GetWindow());

    LoadGlobalSettings();

    m_lastBlinkTime = SDL_GetTicks();
    m_running = true;
}

Application::~Application()
{
    if (m_window && m_window->IsValid())
        SDL_StopTextInput(m_window->GetWindow());

    m_ui.reset();
    m_renderer.reset();
    m_screenBuffer.reset();
    m_document.reset();
    m_window.reset();

    if (m_sdlInitialized)
        SDL_Quit();
}

int Application::Run()
{
    if (!m_running)
        return 1;

    while (m_running)
    {
        // Block until either an event arrives or the next cursor-blink toggle
        // is due. Replaces the old SDL_PollEvent busy-loop that pinned a core.
        Uint64    nextBlink = m_lastBlinkTime + BLINK_INTERVAL_MS;
        Uint64    now       = SDL_GetTicks();
        Sint32    timeoutMs = (nextBlink > now)
                              ? static_cast<Sint32>(nextBlink - now)
                              : 0;

        SDL_Event event;
        if (SDL_WaitEventTimeout(&event, timeoutMs))
        {
            DispatchEvent(event);
            // Drain any other queued events without blocking, so a burst
            // (e.g. key + text-input pair) is handled in one render pass.
            while (SDL_PollEvent(&event))
                DispatchEvent(event);
        }

        Update();

        if (m_needsRedraw)
        {
            Render();
            m_needsRedraw = false;
        }
    }

    return 0;
}

void Application::OpenFile(const std::string& path)
{
    if (m_document->Load(path))
    {
        m_cursor.row    = 0;
        m_cursor.column = 0;
        m_viewportTop   = 0;
        m_viewportLeft  = 0;
        m_selection.Clear();
        m_undoHistory.ClearAll();
        LoadSidecarForCurrentDocument();

        // RTF loads carry document font + size in their header. Apply them
        // so reopening the file restores the look it was saved with —
        // otherwise we'd snap back to whatever the global FontSettings
        // happen to be. ApplyFontSettings rebuilds the renderer, so we
        // only call it when something actually changed.
        FontSettings desired = m_fontSettings;
        int loadedPt = m_document->LoadedPointSize();
        if (loadedPt > 0)
        {
            if      (loadedPt <= 13) desired.size = FontSize::Small;
            else if (loadedPt <= 17) desired.size = FontSize::Medium;
            else if (loadedPt <= 22) desired.size = FontSize::Large;
            else                     desired.size = FontSize::ExtraLarge;
        }
        FontFace mappedFace;
        if (!m_document->LoadedFontFamily().empty()
            && FontFaceFromFamilyName(m_document->LoadedFontFamily().c_str(), mappedFace))
        {
            desired.face = mappedFace;
        }
        if (!(desired == m_fontSettings))
            ApplyFontSettings(desired);

        m_statusMessage = "Ready";
        UpdateWindowTitle();
    }
    else
    {
        m_statusMessage = "Error: cannot open '" + path + "'";
    }
}

// ---------------------------------------------------------------------------
// Event processing
// ---------------------------------------------------------------------------

void Application::DispatchEvent(const SDL_Event& event)
{
    // Each branch sets m_needsRedraw — every event we currently handle
    // can change visible state (cursor, dialog, buffer, layout). Unhandled
    // event types (mouse motion, focus, etc.) fall through with no redraw,
    // so passive mouse movement over the window costs nothing.
    switch (event.type)
    {
        case SDL_EVENT_QUIT:
            RequestExit();
            m_needsRedraw = true;
            break;
        case SDL_EVENT_KEY_DOWN:
            HandleKeyDown(event.key);
            m_needsRedraw = true;
            break;
        case SDL_EVENT_TEXT_INPUT:
            HandleTextInput(event.text.text);
            m_needsRedraw = true;
            break;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            HandleWindowResized(event.window.data1, event.window.data2);
            m_needsRedraw = true;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        {
            int cw = m_renderer ? m_renderer->CellWidth()  : 0;
            int ch = m_renderer ? m_renderer->CellHeight() : 0;
            if (cw > 0 && ch > 0)
            {
                HandleMouseDown(static_cast<int>(event.button.x) / cw,
                                static_cast<int>(event.button.y) / ch,
                                event.button.button);
            }
            // m_needsRedraw is set inside HandleMouseDown only when state
            // actually changes — clicks that hit nothing don't force repaints.
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            int cw = m_renderer ? m_renderer->CellWidth()  : 0;
            int ch = m_renderer ? m_renderer->CellHeight() : 0;
            if (cw > 0 && ch > 0)
            {
                HandleMouseUp(static_cast<int>(event.button.x) / cw,
                              static_cast<int>(event.button.y) / ch,
                              event.button.button);
            }
            break;
        }
        case SDL_EVENT_MOUSE_MOTION:
        {
            // Routed when a menu is active or a scrollbar thumb is being
            // dragged — keeps the no-menu hot path free of per-motion work.
            const bool needMotion =
                m_promptMode == PromptMode::MenuBar
             || m_promptMode == PromptMode::MenuOpen
             || m_scrollbarDragActive;
            if (needMotion)
            {
                int cw = m_renderer ? m_renderer->CellWidth()  : 0;
                int ch = m_renderer ? m_renderer->CellHeight() : 0;
                if (cw > 0 && ch > 0)
                {
                    HandleMouseMotion(static_cast<int>(event.motion.x) / cw,
                                      static_cast<int>(event.motion.y) / ch);
                }
            }
            break;
        }
    }
}

void Application::HandleKeyDown(const SDL_KeyboardEvent& key)
{
    // Menu bar / dropdown navigation
    if (m_promptMode == PromptMode::MenuBar || m_promptMode == PromptMode::MenuOpen)
    {
        HandleMenuKeyDown(key);
        return;
    }

    // Overlay dialogs — any key closes them
    if (m_promptMode == PromptMode::HelpScreen || m_promptMode == PromptMode::AboutScreen)
    {
        m_promptMode = PromptMode::None;
        return;
    }

    // Word Count modal — Space/Tab toggle the status-bar display,
    // Enter/Esc close.
    if (m_promptMode == PromptMode::WordCountDialog)
    {
        switch (key.scancode)
        {
            case SDL_SCANCODE_SPACE:
            case SDL_SCANCODE_TAB:
                ToggleStatusBarWordCount();
                m_swallowNextTextInput = true; // drop the space char
                break;
            case SDL_SCANCODE_RETURN:
            case SDL_SCANCODE_ESCAPE:
                m_promptMode = PromptMode::None;
                break;
            default:
                break;
        }
        return;
    }

    // Font picker — single-column flat preset list.
    if (m_promptMode == PromptMode::FontDialog)
    {
        const int presetCount = FontFaceCount() * FontSizeCount();
        constexpr int visibleRows = 12; // mirrors kFontDialogVisibleRows in RetroUi.cpp
        auto clampScroll = [&]() {
            int maxScroll = std::max(0, presetCount - visibleRows);
            if (m_fontDialogPresetIdx < m_fontDialogScrollTop)
                m_fontDialogScrollTop = m_fontDialogPresetIdx;
            else if (m_fontDialogPresetIdx >= m_fontDialogScrollTop + visibleRows)
                m_fontDialogScrollTop = m_fontDialogPresetIdx - visibleRows + 1;
            m_fontDialogScrollTop = std::clamp(m_fontDialogScrollTop, 0, maxScroll);
        };
        switch (key.scancode)
        {
            case SDL_SCANCODE_UP:
                if (m_fontDialogPresetIdx > 0) --m_fontDialogPresetIdx;
                clampScroll();
                break;
            case SDL_SCANCODE_DOWN:
                if (m_fontDialogPresetIdx < presetCount - 1) ++m_fontDialogPresetIdx;
                clampScroll();
                break;
            case SDL_SCANCODE_PAGEUP:
                m_fontDialogPresetIdx = std::max(0, m_fontDialogPresetIdx - visibleRows);
                clampScroll();
                break;
            case SDL_SCANCODE_PAGEDOWN:
                m_fontDialogPresetIdx = std::min(presetCount - 1,
                                                 m_fontDialogPresetIdx + visibleRows);
                clampScroll();
                break;
            case SDL_SCANCODE_HOME:
                m_fontDialogPresetIdx = 0;
                clampScroll();
                break;
            case SDL_SCANCODE_END:
                m_fontDialogPresetIdx = presetCount - 1;
                clampScroll();
                break;
            case SDL_SCANCODE_RETURN:
                ApplyFontDialogSelection();
                break;
            case SDL_SCANCODE_ESCAPE:
                m_promptMode    = PromptMode::None;
                m_statusMessage = "Ready";
                break;
            default:
                break;
        }
        return;
    }

    // Theme picker — single-column up/down list.
    if (m_promptMode == PromptMode::ThemeDialog)
    {
        const int themeCount = ThemeCount();
        switch (key.scancode)
        {
            case SDL_SCANCODE_UP:
                if (m_themeDialogFocusIdx > 0) --m_themeDialogFocusIdx;
                break;
            case SDL_SCANCODE_DOWN:
                if (m_themeDialogFocusIdx < themeCount - 1) ++m_themeDialogFocusIdx;
                break;
            case SDL_SCANCODE_RETURN:
                ApplyThemeDialogSelection();
                break;
            case SDL_SCANCODE_ESCAPE:
                m_promptMode    = PromptMode::None;
                m_statusMessage = "Ready";
                break;
            default:
                break;
        }
        return;
    }

    // Color / Highlight picker — 4x4 grid (left/right/up/down navigate).
    // Both prompts reuse the same dialog UI; only the commit target differs.
    if (m_promptMode == PromptMode::ColorDialog
        || m_promptMode == PromptMode::HighlightDialog)
    {
        constexpr int kCols = 4;
        const int total = Palette::kCount;
        int idx = m_colorDialogFocusIdx;
        int row = idx / kCols;
        int col = idx % kCols;
        switch (key.scancode)
        {
            case SDL_SCANCODE_LEFT:  if (col > 0) --col; break;
            case SDL_SCANCODE_RIGHT: if (col < kCols - 1 && row * kCols + col + 1 < total) ++col; break;
            case SDL_SCANCODE_UP:    if (row > 0) --row; break;
            case SDL_SCANCODE_DOWN:
                if ((row + 1) * kCols + col < total) ++row;
                break;
            case SDL_SCANCODE_RETURN:
                if (m_promptMode == PromptMode::HighlightDialog)
                    ApplyHighlightDialogSelection();
                else
                    ApplyColorDialogSelection();
                return;
            case SDL_SCANCODE_ESCAPE:
                m_promptMode    = PromptMode::None;
                m_statusMessage = "Ready";
                return;
            default:
                return;
        }
        m_colorDialogFocusIdx = std::clamp(row * kCols + col, 0, total - 1);
        return;
    }

    // Text/confirmation prompts
    if (m_promptMode != PromptMode::None)
    {
        HandlePromptKeyDown(key);
        return;
    }

    const bool ctrl  = (key.mod & SDL_KMOD_CTRL)  != 0;
    const bool shift = (key.mod & SDL_KMOD_SHIFT) != 0;
    const bool alt   = (key.mod & SDL_KMOD_ALT)   != 0;

    // Alt+letter opens the corresponding menu
    if (alt && !ctrl)
    {
        switch (key.scancode)
        {
            case SDL_SCANCODE_F: OpenMenu(0); return;   // File
            case SDL_SCANCODE_E: OpenMenu(1); return;   // Edit
            case SDL_SCANCODE_R: OpenMenu(2); return;   // foRmat
            case SDL_SCANCODE_S: OpenMenu(3); return;   // Search
            case SDL_SCANCODE_V: OpenMenu(4); return;   // View
            case SDL_SCANCODE_P: OpenMenu(5); return;   // Page
            case SDL_SCANCODE_T: OpenMenu(6); return;   // Tools
            case SDL_SCANCODE_O: OpenMenu(7); return;   // Options
            case SDL_SCANCODE_H: OpenMenu(8); return;   // Help
            default: return;
        }
    }

    switch (key.scancode)
    {
        // --- Navigation (all support Shift for selection) ---
        case SDL_SCANCODE_UP:
            UpdateSelection(shift);
            MoveCursorUp();
            m_lastActionWasInsert = false;
            break;

        case SDL_SCANCODE_DOWN:
            UpdateSelection(shift);
            MoveCursorDown();
            m_lastActionWasInsert = false;
            break;

        case SDL_SCANCODE_LEFT:
            if (!shift && m_selection.active && !m_selection.IsEmpty(m_cursor.row, m_cursor.column))
            {
                int sr, sc, er, ec;
                m_selection.GetRange(m_cursor.row, m_cursor.column, sr, sc, er, ec);
                m_cursor.row = sr; m_cursor.column = sc;
                m_selection.Clear();
            }
            else
            {
                UpdateSelection(shift);
                MoveCursorLeft();
            }
            m_lastActionWasInsert = false;
            break;

        case SDL_SCANCODE_RIGHT:
            if (!shift && m_selection.active && !m_selection.IsEmpty(m_cursor.row, m_cursor.column))
            {
                int sr, sc, er, ec;
                m_selection.GetRange(m_cursor.row, m_cursor.column, sr, sc, er, ec);
                m_cursor.row = er; m_cursor.column = ec;
                m_selection.Clear();
            }
            else
            {
                UpdateSelection(shift);
                MoveCursorRight();
            }
            m_lastActionWasInsert = false;
            break;

        case SDL_SCANCODE_HOME:
            UpdateSelection(shift);
            m_cursor.column = 0;
            m_lastActionWasInsert = false;
            break;

        case SDL_SCANCODE_END:
            UpdateSelection(shift);
            m_cursor.column = m_document->Buffer().LineLength(m_cursor.row);
            m_lastActionWasInsert = false;
            break;

        case SDL_SCANCODE_PAGEUP:
            UpdateSelection(shift);
            m_cursor.row = std::max(0, m_cursor.row - m_layout.EDITOR_ROWS);
            ClampCursorToLine();
            ScrollViewport();
            m_lastActionWasInsert = false;
            break;

        case SDL_SCANCODE_PAGEDOWN:
            UpdateSelection(shift);
            m_cursor.row = std::min(m_document->Buffer().LineCount() - 1,
                                    m_cursor.row + m_layout.EDITOR_ROWS);
            ClampCursorToLine();
            ScrollViewport();
            m_lastActionWasInsert = false;
            break;

        // --- Editing ---
        case SDL_SCANCODE_RETURN:
            if (ctrl)
            {
                InsertPageBreak();
                break;
            }
            PushUndoBeforeEdit();
            if (m_selection.active && !m_selection.IsEmpty(m_cursor.row, m_cursor.column))
                EraseSelection();
            m_document->Buffer().InsertNewline(m_cursor.column, m_cursor.row);
            m_cursor.column = 0;
            ++m_cursor.row;
            m_document->MarkDirty();
            ScrollViewport();
            UpdateWindowTitle();
            break;

        case SDL_SCANCODE_TAB:
        {
            PushUndoBeforeEdit();
            if (m_selection.active && !m_selection.IsEmpty(m_cursor.row, m_cursor.column))
                EraseSelection();
            int endRow = 0, endCol = 0;
            m_document->Buffer().InsertText(m_cursor.column, m_cursor.row,
                                            "    ",
                                            CharFormat{m_currentStyle, m_currentFace, m_currentSize, m_currentColor, m_currentHighlight},
                                            endRow, endCol);
            m_cursor.row    = endRow;
            m_cursor.column = endCol;
            m_document->MarkDirty();
            ScrollViewport();
            UpdateWindowTitle();
            break;
        }

        case SDL_SCANCODE_BACKSPACE:
            if (m_selection.active && !m_selection.IsEmpty(m_cursor.row, m_cursor.column))
            {
                PushUndoBeforeEdit();
                EraseSelection();
                m_document->MarkDirty();
                UpdateWindowTitle();
            }
            else if (m_cursor.column > 0)
            {
                PushUndoBeforeEdit();
                m_document->Buffer().Backspace(m_cursor.column, m_cursor.row);
                --m_cursor.column;
                m_document->MarkDirty();
                UpdateWindowTitle();
            }
            else if (m_cursor.row > 0)
            {
                PushUndoBeforeEdit();
                int prevLen = m_document->Buffer().LineLength(m_cursor.row - 1);
                m_document->Buffer().Backspace(0, m_cursor.row);
                --m_cursor.row;
                m_cursor.column = prevLen;
                m_document->MarkDirty();
                ScrollViewport();
                UpdateWindowTitle();
            }
            break;

        case SDL_SCANCODE_DELETE:
            if (m_selection.active && !m_selection.IsEmpty(m_cursor.row, m_cursor.column))
            {
                PushUndoBeforeEdit();
                EraseSelection();
                m_document->MarkDirty();
                UpdateWindowTitle();
            }
            else
            {
                int len   = m_document->Buffer().LineLength(m_cursor.row);
                int lines = m_document->Buffer().LineCount();
                if (m_cursor.column < len || m_cursor.row < lines - 1)
                {
                    PushUndoBeforeEdit();
                    m_document->Buffer().DeleteForward(m_cursor.column, m_cursor.row);
                    m_document->MarkDirty();
                    UpdateWindowTitle();
                }
            }
            break;

        // --- Clipboard & selection ---
        case SDL_SCANCODE_C:
            if (ctrl) CopySelection();
            break;

        case SDL_SCANCODE_X:
            if (ctrl) CutSelection();
            break;

        case SDL_SCANCODE_V:
            if (ctrl) PasteClipboard();
            break;

        case SDL_SCANCODE_A:
            if (ctrl)
            {
                int lastLine = m_document->Buffer().LineCount() - 1;
                m_selection.anchorRow = 0;
                m_selection.anchorCol = 0;
                m_selection.active    = true;
                m_cursor.row    = lastLine;
                m_cursor.column = m_document->Buffer().LineLength(lastLine);
                ScrollViewport();
                m_lastActionWasInsert = false;
            }
            break;

        // --- Undo / Redo ---
        case SDL_SCANCODE_Z:
            if (ctrl) DoUndo();
            break;

        case SDL_SCANCODE_Y:
            if (ctrl) DoRedo();
            break;

        // --- Find ---
        case SDL_SCANCODE_F:
            if (ctrl) StartFindPrompt();
            break;

        case SDL_SCANCODE_F6:
            FindNext();
            break;

        // --- File commands ---
        case SDL_SCANCODE_S:
            if (ctrl && shift) StartSaveAsPrompt();
            else if (ctrl)     SaveDocument();
            break;

        case SDL_SCANCODE_O:
            if (ctrl) StartOpenPrompt();
            break;

        case SDL_SCANCODE_N:
            if (ctrl) NewFile();
            break;

        case SDL_SCANCODE_P:
            if (ctrl) OpenPrintDialog();
            break;

        // --- Per-character formatting (Format menu shortcuts) ---
        case SDL_SCANCODE_B:
            if (ctrl) ToggleBold();
            break;

        case SDL_SCANCODE_I:
            if (ctrl) ToggleItalic();
            break;

        case SDL_SCANCODE_U:
            if (ctrl) ToggleUnderline();
            break;

        case SDL_SCANCODE_F2:
            SaveDocument();
            break;

        case SDL_SCANCODE_F3:
            StartOpenPrompt();
            break;

        // --- Menu / overlay keys ---
        case SDL_SCANCODE_F1:
            m_promptMode = PromptMode::HelpScreen;
            break;

        case SDL_SCANCODE_F5:
            m_statusMessage = "F5 Run not implemented";
            break;

        case SDL_SCANCODE_F10:
            OpenMenuBar();
            break;

        case SDL_SCANCODE_ESCAPE:
            RequestExit();
            break;

        default:
            break;
    }

    // Any cursor change in the editor must keep the cursor visible.
    ScrollViewport();
}

void Application::HandlePromptKeyDown(const SDL_KeyboardEvent& key)
{
    // Confirm dialogs all share Y/N/Esc semantics — keyboard path delegates to
    // the same ResolveConfirmYes / ResolveConfirmNo helpers used by the mouse
    // dispatcher so the two paths can't diverge.
    if (m_promptMode == PromptMode::ConfirmExit         ||
        m_promptMode == PromptMode::ConfirmExitClean    ||
        m_promptMode == PromptMode::ConfirmNew          ||
        m_promptMode == PromptMode::ConfirmWordWrap     ||
        m_promptMode == PromptMode::ConfirmSaveAsRtf)
    {
        switch (key.scancode)
        {
            case SDL_SCANCODE_Y:
                m_swallowNextTextInput = true;
                ResolveConfirmYes();
                break;
            case SDL_SCANCODE_N:
                m_swallowNextTextInput = true;
                ResolveConfirmNo();
                break;
            case SDL_SCANCODE_ESCAPE:
                m_promptMode    = PromptMode::None;
                m_statusMessage = "Ready";
                break;
            default:
                break;
        }
        return;
    }

    // Find dialog has an extra checkbox control reachable via Tab.
    if (m_promptMode == PromptMode::Find)
    {
        switch (key.scancode)
        {
            case SDL_SCANCODE_TAB:
            case SDL_SCANCODE_UP:
            case SDL_SCANCODE_DOWN:
                m_findDialogFocus = 1 - m_findDialogFocus;
                return;
            case SDL_SCANCODE_SPACE:
                if (m_findDialogFocus == 1)
                {
                    m_findCaseInsensitive = !m_findCaseInsensitive;
                    m_swallowNextTextInput = true; // don't let the space land in the query
                    return;
                }
                break; // input field: fall through to text-input path
            case SDL_SCANCODE_RETURN:
                CommitPrompt();
                return;
            case SDL_SCANCODE_ESCAPE:
                CancelPrompt();
                return;
            case SDL_SCANCODE_BACKSPACE:
                if (m_findDialogFocus == 0 && !m_promptText.empty())
                    m_promptText.pop_back();
                return;
            default:
                return;
        }
    }

    // Margins dialog — four numeric fields, Tab cycles, Up/Down adjusts, digits/'.' edit.
    if (m_promptMode == PromptMode::MarginsDialog)
    {
        const bool shift = (key.mod & SDL_KMOD_SHIFT) != 0;
        switch (key.scancode)
        {
            case SDL_SCANCODE_TAB:
                MarginCycleField(shift ? -1 : +1);
                return;
            case SDL_SCANCODE_UP:
                MarginAdjustField(+1);
                return;
            case SDL_SCANCODE_DOWN:
                MarginAdjustField(-1);
                return;
            case SDL_SCANCODE_BACKSPACE:
                MarginBackspace();
                return;
            case SDL_SCANCODE_RETURN:
                CloseMarginsDialog(true);
                return;
            case SDL_SCANCODE_ESCAPE:
                CloseMarginsDialog(false);
                return;
            default:
                return;
        }
    }

    // Print dialog — Tab cycles fields, Up/Down adjusts, Space toggles radio
    // groups, digits/'.' edit numeric fields, Enter prints, Esc cancels.
    if (m_promptMode == PromptMode::PrintDialog)
    {
        const bool shift = (key.mod & SDL_KMOD_SHIFT) != 0;
        switch (key.scancode)
        {
            case SDL_SCANCODE_TAB:
                PrintCycleField(shift ? -1 : +1);
                return;
            case SDL_SCANCODE_UP:
                PrintAdjustField(+1);
                return;
            case SDL_SCANCODE_DOWN:
                PrintAdjustField(-1);
                return;
            case SDL_SCANCODE_LEFT:
                if (m_printFocus == PrintField::Printer)
                    PrintAdjustField(-1);
                return;
            case SDL_SCANCODE_RIGHT:
                if (m_printFocus == PrintField::Printer)
                    PrintAdjustField(+1);
                return;
            case SDL_SCANCODE_SPACE:
                if (m_printFocus == PrintField::RangeMode ||
                    m_printFocus == PrintField::Orientation)
                {
                    PrintAdjustField(+1);
                    m_swallowNextTextInput = true; // drop the literal space
                    return;
                }
                break;
            case SDL_SCANCODE_BACKSPACE:
                PrintBackspace();
                return;
            case SDL_SCANCODE_RETURN:
                ClosePrintDialog(true);
                return;
            case SDL_SCANCODE_ESCAPE:
                ClosePrintDialog(false);
                return;
            default:
                return;
        }
    }

    // Text-input prompts (Open, SaveAs)
    switch (key.scancode)
    {
        case SDL_SCANCODE_RETURN:
            CommitPrompt();
            break;
        case SDL_SCANCODE_ESCAPE:
            CancelPrompt();
            break;
        case SDL_SCANCODE_BACKSPACE:
            if (!m_promptText.empty())
                m_promptText.pop_back();
            break;
        default:
            break;
    }
}

void Application::HandleMenuKeyDown(const SDL_KeyboardEvent& key)
{
    const auto& menus    = GetMenuDefs();
    const int   numMenus = static_cast<int>(menus.size());

    if (m_promptMode == PromptMode::MenuBar)
    {
        switch (key.scancode)
        {
            case SDL_SCANCODE_LEFT:
                m_activeMenu = (m_activeMenu - 1 + numMenus) % numMenus;
                break;
            case SDL_SCANCODE_RIGHT:
                m_activeMenu = (m_activeMenu + 1) % numMenus;
                break;
            case SDL_SCANCODE_RETURN:
            case SDL_SCANCODE_DOWN:
                OpenMenu(m_activeMenu);
                break;
            case SDL_SCANCODE_ESCAPE:
                CloseMenu();
                break;
            default:
                break;
        }
        return;
    }

    // PromptMode::MenuOpen
    switch (key.scancode)
    {
        case SDL_SCANCODE_UP:
            m_activeItem = NextSelectableItem(m_activeMenu, m_activeItem, -1);
            break;
        case SDL_SCANCODE_DOWN:
            m_activeItem = NextSelectableItem(m_activeMenu, m_activeItem, +1);
            break;
        case SDL_SCANCODE_LEFT:
            OpenMenu((m_activeMenu - 1 + numMenus) % numMenus);
            break;
        case SDL_SCANCODE_RIGHT:
            OpenMenu((m_activeMenu + 1) % numMenus);
            break;
        case SDL_SCANCODE_RETURN:
            ExecuteMenuItem(m_activeMenu, m_activeItem);
            break;
        case SDL_SCANCODE_ESCAPE:
            m_promptMode = PromptMode::MenuBar;
            m_activeItem = -1;
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Mouse input
// ---------------------------------------------------------------------------

bool Application::HandleDialogMouseDown(int cellCol, int cellRow)
{
    // Help / About overlays: any click anywhere closes them.
    if (m_promptMode == PromptMode::HelpScreen || m_promptMode == PromptMode::AboutScreen)
    {
        m_promptMode  = PromptMode::None;
        m_needsRedraw = true;
        return true;
    }

    // Font picker — flat preset list.
    if (m_promptMode == PromptMode::FontDialog)
    {
        const int presetCount = FontFaceCount() * FontSizeCount();
        constexpr int visibleRows = 12;
        auto rect = m_ui->FontDialogRect(m_screenColumns);
        if (!rect.Contains(cellCol, cellRow))
        {
            m_promptMode    = PromptMode::None;
            m_statusMessage = "Ready";
            m_needsRedraw   = true;
            return true;
        }
        auto click = m_ui->HitTestFontDialog(cellCol, cellRow, m_screenColumns,
                                             presetCount, m_fontDialogScrollTop);
        int maxScroll = std::max(0, presetCount - visibleRows);
        switch (click.hit)
        {
            case RetroUi::FontHit::PresetRow:
                m_fontDialogPresetIdx = click.index;
                m_needsRedraw         = true;
                break;
            case RetroUi::FontHit::ScrollUp:
                m_fontDialogScrollTop = std::max(0, m_fontDialogScrollTop - 1);
                m_needsRedraw         = true;
                break;
            case RetroUi::FontHit::ScrollDown:
                m_fontDialogScrollTop = std::min(maxScroll,
                                                 m_fontDialogScrollTop + 1);
                m_needsRedraw         = true;
                break;
            case RetroUi::FontHit::ScrollThumb:
                m_scrollbarDragActive       = true;
                m_scrollbarDragOwner        = PromptMode::FontDialog;
                m_scrollbarDragX            = rect.x + rect.w - 2;
                m_scrollbarDragY            = rect.y + 1;
                m_scrollbarDragHeight       = visibleRows;
                m_scrollbarDragTotalItems   = presetCount;
                m_scrollbarDragVisibleItems = visibleRows;
                m_scrollbarDragGrabOffset   = click.grabOffsetInThumb;
                break;
            case RetroUi::FontHit::ScrollTrackAbove:
            case RetroUi::FontHit::ScrollTrackBelow:
                break; // page-jump on track click is a deferred enhancement
            case RetroUi::FontHit::ApplyHint:
                ApplyFontDialogSelection();
                m_needsRedraw = true;
                break;
            case RetroUi::FontHit::CancelHint:
                m_promptMode    = PromptMode::None;
                m_statusMessage = "Ready";
                m_needsRedraw   = true;
                break;
            default:
                break; // inside dialog but on dead area — no-op
        }
        return true;
    }

    // Theme picker
    if (m_promptMode == PromptMode::ThemeDialog)
    {
        const int themeCount = ThemeCount();
        auto rect = m_ui->ThemeDialogRect(m_screenColumns, themeCount);
        if (!rect.Contains(cellCol, cellRow))
        {
            m_promptMode    = PromptMode::None;
            m_statusMessage = "Ready";
            m_needsRedraw   = true;
            return true;
        }
        auto click = m_ui->HitTestThemeDialog(cellCol, cellRow, m_screenColumns,
                                              themeCount);
        switch (click.hit)
        {
            case RetroUi::ThemeHit::Row:
                m_themeDialogFocusIdx = click.index;
                ApplyThemeDialogSelection();
                m_needsRedraw = true;
                break;
            case RetroUi::ThemeHit::OkHint:
                ApplyThemeDialogSelection();
                m_needsRedraw = true;
                break;
            case RetroUi::ThemeHit::CancelHint:
                m_promptMode    = PromptMode::None;
                m_statusMessage = "Ready";
                m_needsRedraw   = true;
                break;
            default:
                break;
        }
        return true;
    }

    // Color / Highlight picker — same UI, different commit target.
    if (m_promptMode == PromptMode::ColorDialog
        || m_promptMode == PromptMode::HighlightDialog)
    {
        const bool isHighlight = (m_promptMode == PromptMode::HighlightDialog);
        auto applyClick = [&]() {
            if (isHighlight) ApplyHighlightDialogSelection();
            else             ApplyColorDialogSelection();
        };

        auto rect = m_ui->ColorDialogRect(m_screenColumns);
        if (!rect.Contains(cellCol, cellRow))
        {
            m_promptMode    = PromptMode::None;
            m_statusMessage = "Ready";
            m_needsRedraw   = true;
            return true;
        }
        auto click = m_ui->HitTestColorDialog(cellCol, cellRow, m_screenColumns);
        switch (click.hit)
        {
            case RetroUi::ColorHit::Swatch:
                m_colorDialogFocusIdx = click.index;
                applyClick();
                m_needsRedraw = true;
                break;
            case RetroUi::ColorHit::OkHint:
                applyClick();
                m_needsRedraw = true;
                break;
            case RetroUi::ColorHit::CancelHint:
                m_promptMode    = PromptMode::None;
                m_statusMessage = "Ready";
                m_needsRedraw   = true;
                break;
            default:
                break;
        }
        return true;
    }

    // Word Count dialog
    if (m_promptMode == PromptMode::WordCountDialog)
    {
        auto rect = m_ui->WordCountDialogRect(m_screenColumns);
        if (!rect.Contains(cellCol, cellRow))
        {
            m_promptMode  = PromptMode::None;
            m_needsRedraw = true;
            return true;
        }
        auto hit = m_ui->HitTestWordCountDialog(cellCol, cellRow, m_screenColumns);
        if (hit == RetroUi::WordCountHit::Checkbox)
        {
            ToggleStatusBarWordCount();
            m_needsRedraw = true;
        }
        else if (hit == RetroUi::WordCountHit::CloseHint)
        {
            m_promptMode  = PromptMode::None;
            m_needsRedraw = true;
        }
        return true;
    }

    // Find dialog (input field + case-insensitive checkbox)
    if (m_promptMode == PromptMode::Find)
    {
        auto rect = m_ui->FindDialogRect(m_screenColumns);
        if (!rect.Contains(cellCol, cellRow))
        {
            CancelPrompt();
            m_needsRedraw = true;
            return true;
        }
        auto hit = m_ui->HitTestFindDialog(cellCol, cellRow, m_screenColumns);
        switch (hit)
        {
            case RetroUi::FindHit::InputField:
                m_findDialogFocus = 0;
                m_needsRedraw     = true;
                break;
            case RetroUi::FindHit::Checkbox:
                m_findDialogFocus     = 1;
                m_findCaseInsensitive = !m_findCaseInsensitive;
                m_needsRedraw         = true;
                break;
            case RetroUi::FindHit::OkHint:
                CommitPrompt();
                m_needsRedraw = true;
                break;
            case RetroUi::FindHit::CancelHint:
                CancelPrompt();
                m_needsRedraw = true;
                break;
            default:
                break;
        }
        return true;
    }

    // Plain input dialogs (Open, SaveAs, AddWord, RemoveWord, CheckWord)
    if (m_promptMode == PromptMode::Open             ||
        m_promptMode == PromptMode::SaveAs           ||
        m_promptMode == PromptMode::AddWordDialog    ||
        m_promptMode == PromptMode::RemoveWordDialog ||
        m_promptMode == PromptMode::CheckWordDialog)
    {
        auto rect = m_ui->InputDialogRect(m_screenColumns);
        if (!rect.Contains(cellCol, cellRow))
        {
            CancelPrompt();
            m_needsRedraw = true;
            return true;
        }
        auto hit = m_ui->HitTestInputDialog(cellCol, cellRow, m_screenColumns);
        if (hit == RetroUi::InputHit::OkHint)
        {
            CommitPrompt();
            m_needsRedraw = true;
        }
        else if (hit == RetroUi::InputHit::CancelHint)
        {
            CancelPrompt();
            m_needsRedraw = true;
        }
        // Inside the dialog but not on a hint token: no-op (input field is
        // already focused; nothing else to switch to).
        return true;
    }

    // Y/N confirm dialogs — outside-click cancels, hint clicks resolve.
    if (m_promptMode == PromptMode::ConfirmExit         ||
        m_promptMode == PromptMode::ConfirmExitClean    ||
        m_promptMode == PromptMode::ConfirmNew          ||
        m_promptMode == PromptMode::ConfirmWordWrap     ||
        m_promptMode == PromptMode::ConfirmSaveAsRtf)
    {
        auto rect = m_ui->ConfirmDialogRect(m_screenColumns);
        if (!rect.Contains(cellCol, cellRow))
        {
            m_promptMode    = PromptMode::None;
            m_statusMessage = "Ready";
            m_needsRedraw   = true;
            return true;
        }
        // Reproduce the hint string rendered in Render() for token hit-test.
        std::string hint;
        switch (m_promptMode)
        {
            case PromptMode::ConfirmExit:
                hint = "[Y] Save     [N] Discard     [Esc] Cancel";
                break;
            case PromptMode::ConfirmWordWrap:
                hint = "[Y] On      [N] Off      [Esc] Cancel";
                break;
            case PromptMode::ConfirmSaveAsRtf:
                hint = "[Y] Save .rtf  [N] Save .txt  [Esc] Cancel";
                break;
            default:
                hint.clear(); // DrawConfirmDialog falls back to "[Y] Yes      [N] No"
                break;
        }
        auto hit = m_ui->HitTestConfirmDialog(cellCol, cellRow, m_screenColumns, hint);
        switch (hit)
        {
            case RetroUi::ConfirmHit::Yes:
                ResolveConfirmYes();
                m_needsRedraw = true;
                break;
            case RetroUi::ConfirmHit::No:
                ResolveConfirmNo();
                m_needsRedraw = true;
                break;
            case RetroUi::ConfirmHit::Cancel:
                m_promptMode    = PromptMode::None;
                m_statusMessage = "Ready";
                m_needsRedraw   = true;
                break;
            default:
                break;
        }
        return true;
    }

    // Margins dialog
    if (m_promptMode == PromptMode::MarginsDialog)
    {
        auto rect = m_ui->MarginsDialogRect(m_screenColumns);
        if (!rect.Contains(cellCol, cellRow))
        {
            CloseMarginsDialog(false);
            m_needsRedraw = true;
            return true;
        }
        auto hit = m_ui->HitTestMarginsDialog(cellCol, cellRow, m_screenColumns);
        switch (hit)
        {
            case RetroUi::MarginsHit::Top:    m_marginFocusIdx = 0; m_needsRedraw = true; break;
            case RetroUi::MarginsHit::Bottom: m_marginFocusIdx = 1; m_needsRedraw = true; break;
            case RetroUi::MarginsHit::Left:   m_marginFocusIdx = 2; m_needsRedraw = true; break;
            case RetroUi::MarginsHit::Right:  m_marginFocusIdx = 3; m_needsRedraw = true; break;
            case RetroUi::MarginsHit::OkHint:     CloseMarginsDialog(true);  m_needsRedraw = true; break;
            case RetroUi::MarginsHit::CancelHint: CloseMarginsDialog(false); m_needsRedraw = true; break;
            default: break;
        }
        return true;
    }

    // Print dialog
    if (m_promptMode == PromptMode::PrintDialog)
    {
        auto rect = m_ui->PrintDialogRect(m_screenColumns);
        if (!rect.Contains(cellCol, cellRow))
        {
            ClosePrintDialog(false);
            m_needsRedraw = true;
            return true;
        }
        auto hit = m_ui->HitTestPrintDialog(cellCol, cellRow, m_screenColumns);
        switch (hit)
        {
            case RetroUi::PrintHit::PrinterPrev:
                m_printFocus = PrintField::Printer;
                PrintAdjustField(-1);
                m_needsRedraw = true;
                break;
            case RetroUi::PrintHit::PrinterNext:
                m_printFocus = PrintField::Printer;
                PrintAdjustField(+1);
                m_needsRedraw = true;
                break;
            case RetroUi::PrintHit::Copies:
                m_printFocus = PrintField::Copies;
                m_needsRedraw = true;
                break;
            case RetroUi::PrintHit::RangeAll:
                m_printFocus = PrintField::RangeMode;
                m_printRequest.allPages = true;
                m_needsRedraw = true;
                break;
            case RetroUi::PrintHit::RangeCustom:
                m_printFocus = PrintField::RangeMode;
                m_printRequest.allPages = false;
                m_needsRedraw = true;
                break;
            case RetroUi::PrintHit::RangeFrom:
                m_printFocus = PrintField::RangeFrom;
                m_needsRedraw = true;
                break;
            case RetroUi::PrintHit::RangeTo:
                m_printFocus = PrintField::RangeTo;
                m_needsRedraw = true;
                break;
            case RetroUi::PrintHit::Portrait:
                m_printFocus = PrintField::Orientation;
                m_printRequest.orientation = PrintOrientation::Portrait;
                m_needsRedraw = true;
                break;
            case RetroUi::PrintHit::Landscape:
                m_printFocus = PrintField::Orientation;
                m_printRequest.orientation = PrintOrientation::Landscape;
                m_needsRedraw = true;
                break;
            case RetroUi::PrintHit::MarginTop:
                m_printFocus = PrintField::MarginTop;
                m_needsRedraw = true;
                break;
            case RetroUi::PrintHit::MarginBottom:
                m_printFocus = PrintField::MarginBottom;
                m_needsRedraw = true;
                break;
            case RetroUi::PrintHit::MarginLeft:
                m_printFocus = PrintField::MarginLeft;
                m_needsRedraw = true;
                break;
            case RetroUi::PrintHit::MarginRight:
                m_printFocus = PrintField::MarginRight;
                m_needsRedraw = true;
                break;
            case RetroUi::PrintHit::OkHint:
                ClosePrintDialog(true);
                m_needsRedraw = true;
                break;
            case RetroUi::PrintHit::CancelHint:
                ClosePrintDialog(false);
                m_needsRedraw = true;
                break;
            default:
                break;
        }
        return true;
    }

    return false; // no dialog active — caller falls through to menu handling
}

void Application::HandleMouseDown(int cellCol, int cellRow, Uint8 button)
{
    if (button != SDL_BUTTON_LEFT) return;

    // Dialogs take priority — they're modal.
    if (HandleDialogMouseDown(cellCol, cellRow)) return;

    const bool menuOpen = (m_promptMode == PromptMode::MenuOpen);
    const bool menuBar  = (m_promptMode == PromptMode::MenuBar);

    // Click on the menu bar row → open / switch / toggle / no-op
    if (cellRow == m_layout.ROW_MENUBAR)
    {
        int hit = m_ui->HitTestMenuBar(cellCol);
        if (hit < 0)
        {
            if (menuOpen || menuBar) { CloseMenu(); m_needsRedraw = true; }
            return;
        }
        if (menuOpen && hit == m_activeMenu)
        {
            CloseMenu();
            m_needsRedraw = true;
        }
        else
        {
            OpenMenu(hit);
            m_needsRedraw = true;
        }
        return;
    }

    // Click inside a dropdown → activate the item; click outside the dropdown
    // while one is open → close it.
    if (menuOpen)
    {
        int item = m_ui->HitTestDropdownItem(
            m_activeMenu, cellCol, cellRow, m_screenColumns,
            m_wordWrap, m_showWordCount,
            m_spellCheckEnabled, m_highlightMisspelled);
        if (item >= 0)
        {
            // ExecuteMenuItem already filters separators (empty label).
            ExecuteMenuItem(m_activeMenu, item);
            m_needsRedraw = true;
        }
        else
        {
            CloseMenu();
            m_needsRedraw = true;
        }
        return;
    }

    // Menu-bar focused but no dropdown open: click anywhere off the bar
    // cancels the focused state.
    if (menuBar)
    {
        CloseMenu();
        m_needsRedraw = true;
    }
}

void Application::HandleMouseUp(int /*cellCol*/, int /*cellRow*/, Uint8 button)
{
    if (button != SDL_BUTTON_LEFT) return;
    if (m_scrollbarDragActive)
    {
        m_scrollbarDragActive = false;
        m_scrollbarDragOwner  = PromptMode::None;
    }
}

void Application::HandleMouseMotion(int cellCol, int cellRow)
{
    // Scrollbar thumb drag — recompute scrollTop from the current mouse row,
    // dispatched by which dialog initiated the drag.
    if (m_scrollbarDragActive)
    {
        // Owning dialog closed mid-drag (e.g., Esc) — drop the drag silently.
        if (m_promptMode != m_scrollbarDragOwner)
        {
            m_scrollbarDragActive = false;
            m_scrollbarDragOwner  = PromptMode::None;
            return;
        }
        int newScrollTop = m_ui->ComputeScrollTopFromThumbDrag(
            cellRow,
            m_scrollbarDragY, m_scrollbarDragHeight,
            m_scrollbarDragTotalItems, m_scrollbarDragVisibleItems,
            m_scrollbarDragGrabOffset);

        switch (m_scrollbarDragOwner)
        {
            case PromptMode::FontDialog:
                if (newScrollTop != m_fontDialogScrollTop)
                {
                    m_fontDialogScrollTop = newScrollTop;
                    m_needsRedraw         = true;
                }
                break;
            default:
                break;
        }
        return;
    }

    // Menu hover (gated in DispatchEvent so we only get here with a menu open).
    if (cellRow == m_layout.ROW_MENUBAR)
    {
        int hit = m_ui->HitTestMenuBar(cellCol);
        if (hit < 0 || hit == m_activeMenu) return;

        if (m_promptMode == PromptMode::MenuOpen)
        {
            // Auto-switch dropdown to the menu under the cursor.
            OpenMenu(hit);
            m_needsRedraw = true;
        }
        else // MenuBar
        {
            m_activeMenu  = hit;
            m_needsRedraw = true;
        }
        return;
    }

    if (m_promptMode == PromptMode::MenuOpen)
    {
        int item = m_ui->HitTestDropdownItem(
            m_activeMenu, cellCol, cellRow, m_screenColumns,
            m_wordWrap, m_showWordCount,
            m_spellCheckEnabled, m_highlightMisspelled);
        if (item >= 0 && item != m_activeItem)
        {
            // Skip separators — keep the previous highlighted item.
            const auto& menus = GetMenuDefs();
            if (m_activeMenu >= 0 && m_activeMenu < static_cast<int>(menus.size())
                && item < static_cast<int>(menus[m_activeMenu].items.size())
                && !menus[m_activeMenu].items[item].label.empty())
            {
                m_activeItem  = item;
                m_needsRedraw = true;
            }
        }
    }
}

void Application::HandleTextInput(const char* text)
{
    // Swallow the TEXT_INPUT that immediately follows a Y/N keypress used to
    // close a confirm dialog — otherwise the character lands in the document.
    if (m_swallowNextTextInput)
    {
        m_swallowNextTextInput = false;
        return;
    }

    if (m_promptMode != PromptMode::None)
    {
        if (m_promptMode == PromptMode::Open            ||
            m_promptMode == PromptMode::SaveAs          ||
            m_promptMode == PromptMode::AddWordDialog   ||
            m_promptMode == PromptMode::RemoveWordDialog||
            m_promptMode == PromptMode::CheckWordDialog)
        {
            m_promptText += text;
        }
        else if (m_promptMode == PromptMode::Find && m_findDialogFocus == 0)
        {
            m_promptText += text;
        }
        else if (m_promptMode == PromptMode::PrintDialog)
        {
            for (const char* p = text; *p; ++p)
                PrintTextEdit(*p);
        }
        else if (m_promptMode == PromptMode::MarginsDialog)
        {
            for (const char* p = text; *p; ++p)
                MarginTextEdit(*p);
        }
        return;
    }

    for (const char* p = text; *p; ++p)
    {
        unsigned char uch = static_cast<unsigned char>(*p);
        if (uch >= 32 && uch < 128)
        {
            if (m_selection.active && !m_selection.IsEmpty(m_cursor.row, m_cursor.column))
            {
                PushUndoBeforeEdit();
                EraseSelection();
                m_lastActionWasInsert = true;
            }
            EnsureUndoBeforeInsert();
            m_document->Buffer().InsertChar(m_cursor.column, m_cursor.row, *p,
                                            CharFormat{m_currentStyle, m_currentFace, m_currentSize, m_currentColor, m_currentHighlight});
            ++m_cursor.column;
            m_document->MarkDirty();
            UpdateWindowTitle();

            // Spell check: if the user just typed a word-boundary character,
            // check the word that immediately precedes the boundary.
            if (m_spellCheckEnabled)
            {
                bool isLetter = std::isalpha(uch) != 0 || uch == '\'';
                if (!isLetter)
                    CheckJustCompletedWord();
            }
        }
    }

    // Typing past the visible window must scroll horizontally.
    ScrollViewport();
}

// ---------------------------------------------------------------------------
// Navigation helpers
// ---------------------------------------------------------------------------

void Application::UpdateSelection(bool shift)
{
    if (shift)
    {
        if (!m_selection.active)
            m_selection.Activate(m_cursor.row, m_cursor.column);
    }
    else
    {
        m_selection.Clear();
    }
}

int Application::NavigationWrapWidth() const
{
    return WysiwygRenderer::ComputeCharsPerLine(
        m_fontSettings.face,
        FontSizePoints(m_fontSettings.size),
        m_margins.leftIn, m_margins.rightIn);
}

namespace
{
    // Finds the index in `layout` of the visual line containing (row, col).
    // When col equals the segment's endCol AND that segment is not the
    // last one for `row`, we treat the cursor as being at the start of the
    // next segment — except at end-of-buffer-row, where it sits at the
    // segment's tail.
    int FindVisualLine(const std::vector<WysiwygRenderer::VisualLine>& layout,
                       int row, int col)
    {
        int firstForRow = -1;
        int lastForRow  = -1;
        for (int i = 0; i < static_cast<int>(layout.size()); ++i)
        {
            if (layout[i].bufferRow == row)
            {
                if (firstForRow < 0) firstForRow = i;
                lastForRow = i;
            }
            else if (firstForRow >= 0)
            {
                break;
            }
        }
        if (firstForRow < 0) return -1;
        for (int i = firstForRow; i <= lastForRow; ++i)
        {
            const auto& vl = layout[i];
            if (col >= vl.startCol && col < vl.endCol) return i;
            if (col == vl.endCol)
            {
                // Cursor at exact end of a non-last segment belongs to the
                // current segment (consumed-space position); cursor at end
                // of the last segment also belongs here.
                return i;
            }
        }
        return lastForRow;
    }

    // Returns the source column in `vl` whose pixel position is closest to
    // targetX. Ties prefer the lower column.
    int NearestColumnByX(const WysiwygRenderer::VisualLine& vl, int targetX)
    {
        int best     = vl.startCol;
        int bestDist = -1;
        for (int k = 0; k < static_cast<int>(vl.charXs.size()); ++k)
        {
            int dist = std::abs(vl.charXs[k] - targetX);
            if (bestDist < 0 || dist < bestDist)
            {
                bestDist = dist;
                best     = vl.startCol + k;
            }
        }
        return best;
    }
}

void Application::MoveCursorUp()
{
    // Use the renderer's actual visual layout so Up/Down lands at the
    // correct visual column even in mixed-size or proportional paragraphs.
    if (m_wysiwyg && m_document)
    {
        auto ctx    = BuildWysiwygDrawContext();
        auto layout = m_wysiwyg->ComputeVisualLayout(ctx);
        if (!layout.empty())
        {
            int v = FindVisualLine(layout, m_cursor.row, m_cursor.column);
            if (v > 0)
            {
                int offsetInSeg = std::clamp(m_cursor.column - layout[v].startCol,
                                             0,
                                             static_cast<int>(layout[v].charXs.size()) - 1);
                int targetX = layout[v].charXs[offsetInSeg];
                const auto& prev = layout[v - 1];
                m_cursor.row    = prev.bufferRow;
                m_cursor.column = NearestColumnByX(prev, targetX);
                ClampCursorToLine();
                ScrollViewport();
            }
            return;
        }
    }

    int width = NavigationWrapWidth();
    if (width <= 0)
    {
        if (m_cursor.row > 0)
        {
            --m_cursor.row;
            ClampCursorToLine();
            ScrollViewport();
        }
        return;
    }

    // Wrap-aware (non-WYSIWYG, word-wrap mode): column-based fallback.
    const std::string& curLine = m_document->Buffer().Line(m_cursor.row);
    auto starts = ComputeWrapStarts(curLine, width);
    int  segIdx = WrapSegmentForColumn(starts, m_cursor.column);
    int  offset = m_cursor.column - starts[segIdx];

    if (segIdx > 0)
    {
        int destStart = starts[segIdx - 1];
        int destLen   = starts[segIdx] - destStart;
        m_cursor.column = destStart + std::min(offset, destLen);
    }
    else if (m_cursor.row > 0)
    {
        --m_cursor.row;
        const std::string& prevLine = m_document->Buffer().Line(m_cursor.row);
        auto prevStarts = ComputeWrapStarts(prevLine, width);
        int  lastSeg    = static_cast<int>(prevStarts.size()) - 1;
        int  destStart  = prevStarts[lastSeg];
        int  destLen    = static_cast<int>(prevLine.size()) - destStart;
        m_cursor.column = destStart + std::min(offset, destLen);
    }
    ClampCursorToLine();
    ScrollViewport();
}

void Application::MoveCursorDown()
{
    if (m_wysiwyg && m_document)
    {
        auto ctx    = BuildWysiwygDrawContext();
        auto layout = m_wysiwyg->ComputeVisualLayout(ctx);
        if (!layout.empty())
        {
            int v = FindVisualLine(layout, m_cursor.row, m_cursor.column);
            if (v >= 0 && v + 1 < static_cast<int>(layout.size()))
            {
                int offsetInSeg = std::clamp(m_cursor.column - layout[v].startCol,
                                             0,
                                             static_cast<int>(layout[v].charXs.size()) - 1);
                int targetX = layout[v].charXs[offsetInSeg];
                const auto& next = layout[v + 1];
                m_cursor.row    = next.bufferRow;
                m_cursor.column = NearestColumnByX(next, targetX);
                ClampCursorToLine();
                ScrollViewport();
            }
            return;
        }
    }

    int width = NavigationWrapWidth();
    if (width <= 0)
    {
        if (m_cursor.row < m_document->Buffer().LineCount() - 1)
        {
            ++m_cursor.row;
            ClampCursorToLine();
            ScrollViewport();
        }
        return;
    }

    const std::string& curLine = m_document->Buffer().Line(m_cursor.row);
    auto starts = ComputeWrapStarts(curLine, width);
    int  segIdx = WrapSegmentForColumn(starts, m_cursor.column);
    int  offset = m_cursor.column - starts[segIdx];

    if (segIdx + 1 < static_cast<int>(starts.size()))
    {
        int destStart = starts[segIdx + 1];
        int destEnd   = (segIdx + 2 < static_cast<int>(starts.size()))
                        ? starts[segIdx + 2]
                        : static_cast<int>(curLine.size());
        int destLen   = destEnd - destStart;
        m_cursor.column = destStart + std::min(offset, destLen);
    }
    else if (m_cursor.row < m_document->Buffer().LineCount() - 1)
    {
        ++m_cursor.row;
        const std::string& nextLine = m_document->Buffer().Line(m_cursor.row);
        auto nextStarts = ComputeWrapStarts(nextLine, width);
        int  destEnd    = (nextStarts.size() > 1)
                          ? nextStarts[1]
                          : static_cast<int>(nextLine.size());
        m_cursor.column = std::min(offset, destEnd);
    }
    ClampCursorToLine();
    ScrollViewport();
}

void Application::MoveCursorLeft()
{
    if (m_cursor.column > 0)
        --m_cursor.column;
    else if (m_cursor.row > 0)
    {
        --m_cursor.row;
        m_cursor.column = m_document->Buffer().LineLength(m_cursor.row);
        ScrollViewport();
    }
}

void Application::MoveCursorRight()
{
    int len = m_document->Buffer().LineLength(m_cursor.row);
    if (m_cursor.column < len)
        ++m_cursor.column;
    else if (m_cursor.row < m_document->Buffer().LineCount() - 1)
    {
        ++m_cursor.row;
        m_cursor.column = 0;
        ScrollViewport();
    }
}

// ---------------------------------------------------------------------------
// Undo helpers
// ---------------------------------------------------------------------------

void Application::PushUndoBeforeEdit()
{
    m_undoHistory.PushEdit(m_document->Buffer(), m_cursor.row, m_cursor.column);
    m_lastActionWasInsert = false;
}

void Application::EnsureUndoBeforeInsert()
{
    if (!m_lastActionWasInsert)
    {
        m_undoHistory.PushEdit(m_document->Buffer(), m_cursor.row, m_cursor.column);
        m_lastActionWasInsert = true;
    }
}

void Application::ApplyUndoState(const RichUndoState& s)
{
    m_document->Buffer().SetLines(s.lines, s.formats, s.pageBreaks);
    m_cursor.row    = s.cursorRow;
    m_cursor.column = s.cursorCol;
    m_selection.Clear();
    m_document->MarkDirty();
    ClampCursorToLine();
    ScrollViewport();
    UpdateWindowTitle();
    m_lastActionWasInsert = false;
}

void Application::DoUndo()
{
    if (!m_undoHistory.CanUndo())
    {
        m_statusMessage = "Nothing to undo";
        return;
    }
    ApplyUndoState(m_undoHistory.Undo(m_document->Buffer(), m_cursor.row, m_cursor.column));
    m_statusMessage = "Undo";
}

void Application::DoRedo()
{
    if (!m_undoHistory.CanRedo())
    {
        m_statusMessage = "Nothing to redo";
        return;
    }
    ApplyUndoState(m_undoHistory.Redo(m_document->Buffer(), m_cursor.row, m_cursor.column));
    m_statusMessage = "Redo";
}

// ---------------------------------------------------------------------------
// Editing helpers
// ---------------------------------------------------------------------------

void Application::EraseSelection()
{
    if (!m_selection.active || m_selection.IsEmpty(m_cursor.row, m_cursor.column))
    {
        m_selection.Clear();
        return;
    }

    int startRow, startCol, endRow, endCol;
    m_selection.GetRange(m_cursor.row, m_cursor.column,
                         startRow, startCol, endRow, endCol);

    m_document->Buffer().DeleteRange(startRow, startCol, endRow, endCol);
    m_cursor.row    = startRow;
    m_cursor.column = startCol;
    m_selection.Clear();
    ScrollViewport();
}

// ---------------------------------------------------------------------------
// Clipboard
// ---------------------------------------------------------------------------

void Application::CopySelection()
{
    if (!m_selection.active || m_selection.IsEmpty(m_cursor.row, m_cursor.column))
        return;

    int startRow, startCol, endRow, endCol;
    m_selection.GetRange(m_cursor.row, m_cursor.column,
                         startRow, startCol, endRow, endCol);

    std::string text = m_document->Buffer().Text().GetText(startRow, startCol, endRow, endCol);
    SDL_SetClipboardText(text.c_str());
    m_statusMessage = "Copied";
}

void Application::CutSelection()
{
    if (!m_selection.active || m_selection.IsEmpty(m_cursor.row, m_cursor.column))
        return;

    CopySelection();
    PushUndoBeforeEdit();
    EraseSelection();
    m_document->MarkDirty();
    UpdateWindowTitle();
    m_statusMessage = "Cut";
}

void Application::PasteClipboard()
{
    if (!SDL_HasClipboardText())
        return;

    char* raw = SDL_GetClipboardText();
    if (!raw) return;
    std::string text(raw);
    SDL_free(raw);

    if (text.empty()) return;

    PushUndoBeforeEdit();

    if (m_selection.active && !m_selection.IsEmpty(m_cursor.row, m_cursor.column))
        EraseSelection();

    int endRow = 0, endCol = 0;
    m_document->Buffer().InsertText(m_cursor.column, m_cursor.row, text,
                                    CharFormat{m_currentStyle, m_currentFace, m_currentSize, m_currentColor, m_currentHighlight},
                                    endRow, endCol);
    m_cursor.row    = endRow;
    m_cursor.column = endCol;
    m_selection.Clear();
    m_document->MarkDirty();
    ScrollViewport();
    UpdateWindowTitle();
    m_statusMessage = "Pasted";
}

// ---------------------------------------------------------------------------
// Find
// ---------------------------------------------------------------------------

void Application::StartFindPrompt()
{
    m_promptMode      = PromptMode::Find;
    m_promptText      = m_findQuery;
    m_findDialogFocus = 0;   // start on the input field; case toggle persists
    m_statusMessage.clear();
}

void Application::DoFind(const std::string& query)
{
    if (query.empty()) return;
    m_findQuery = query;

    int foundRow = 0, foundCol = 0;
    if (m_document->Buffer().Text().FindNext(query, m_findFromRow, m_findFromCol,
                                              foundRow, foundCol,
                                              m_findCaseInsensitive))
    {
        m_cursor.row    = foundRow;
        m_cursor.column = foundCol + static_cast<int>(query.size());
        m_selection.active    = true;
        m_selection.anchorRow = foundRow;
        m_selection.anchorCol = foundCol;
        m_findFromRow = foundRow;
        m_findFromCol = foundCol;
        m_statusMessage = "Found";
        ScrollViewport();
    }
    else
    {
        m_statusMessage = "Not found: " + query;
        m_findFromRow = 0;
        m_findFromCol = 0;
    }
    m_lastActionWasInsert = false;
}

void Application::FindNext()
{
    if (m_findQuery.empty())
    {
        StartFindPrompt();
        return;
    }
    DoFind(m_findQuery);
}

// ---------------------------------------------------------------------------
// File operations
// ---------------------------------------------------------------------------

void Application::NewFile()
{
    m_document = std::make_unique<RichFileDocument>();
    m_cursor.row    = 0;
    m_cursor.column = 0;
    m_viewportTop   = 0;
    m_viewportLeft  = 0;
    m_selection.Clear();
    m_undoHistory.ClearAll();
    m_lastActionWasInsert = false;
    m_statusMessage = "New file";
    UpdateWindowTitle();
}

void Application::StartOpenPrompt()
{
    m_promptMode = PromptMode::Open;
    m_promptText.clear();
    m_statusMessage.clear();
}

void Application::StartSaveAsPrompt()
{
    m_promptMode = PromptMode::SaveAs;
    m_promptText.clear();
    m_statusMessage.clear();
}

void Application::CommitPrompt()
{
    PromptMode mode = m_promptMode;
    m_promptMode = PromptMode::None;

    if (mode == PromptMode::Open)
    {
        if (!m_promptText.empty())
            OpenFile(m_promptText);
    }
    else if (mode == PromptMode::SaveAs)
    {
        if (!m_promptText.empty())
        {
            if (m_document->SaveAs(m_promptText,
                                   m_fontSettings.face,
                                   FontSizePoints(m_fontSettings.size),
                                   CurrentRtfPage()))
            {
                WriteSidecarForCurrentDocument();
                m_statusMessage = "Saved.";
                UpdateWindowTitle();
                if (m_exitAfterSave)
                {
                    m_exitAfterSave = false;
                    m_running       = false;
                }
            }
            else
            {
                m_statusMessage = "Error: could not save file";
                m_exitAfterSave = false;
            }
        }
        else
        {
            m_exitAfterSave = false;
        }
    }
    else if (mode == PromptMode::Find)
    {
        m_findFromRow = m_cursor.row;
        m_findFromCol = m_cursor.column;
        DoFind(m_promptText);
    }
    else if (mode == PromptMode::AddWordDialog)
    {
        if (!m_promptText.empty())
        {
            if (m_dictionary.AddWord(m_promptText))
            {
                SaveUserDictionary();
                m_statusMessage = "Added '" + m_promptText + "' to dictionary";
            }
            else
            {
                m_statusMessage = "'" + m_promptText + "' is already in the dictionary";
            }
        }
    }
    else if (mode == PromptMode::RemoveWordDialog)
    {
        if (!m_promptText.empty())
        {
            if (m_dictionary.RemoveWord(m_promptText))
            {
                SaveUserDictionary();
                m_statusMessage = "Removed '" + m_promptText + "' from dictionary";
            }
            else
            {
                m_statusMessage = "'" + m_promptText + "' is not in the dictionary";
            }
        }
    }
    else if (mode == PromptMode::CheckWordDialog)
    {
        if (!m_promptText.empty())
        {
            m_statusMessage = m_dictionary.Contains(m_promptText)
                ? "'" + m_promptText + "' is in the dictionary"
                : "'" + m_promptText + "' is NOT in the dictionary";
        }
    }

    m_promptText.clear();
}

void Application::CancelPrompt()
{
    m_promptMode = PromptMode::None;
    m_promptText.clear();
    m_statusMessage   = "Ready";
    m_exitAfterSave   = false;
}

void Application::RequestExit()
{
    if (m_document->IsDirty())
        m_promptMode = PromptMode::ConfirmExit;
    else
        m_promptMode = PromptMode::ConfirmExitClean;
}

bool Application::SaveDocument()
{
    if (m_document->Filename().empty())
    {
        StartSaveAsPrompt();
        return false;
    }
    // .txt (or any non-.rtf) target + formatting present → prompt user to
    // choose between saving as .rtf (preserves formatting) or saving as
    // plain text (drops it). Without the prompt a Ctrl+S would silently
    // discard bold/italic/underline/strikethrough state.
    if (!RichFileDocument::IsRtfPath(m_document->Filename())
        && m_document->Buffer().HasAnyFormatting())
    {
        m_promptMode = PromptMode::ConfirmSaveAsRtf;
        m_statusMessage.clear();
        return false;
    }
    if (m_document->Save(m_fontSettings.face,
                         FontSizePoints(m_fontSettings.size),
                         CurrentRtfPage()))
    {
        WriteSidecarForCurrentDocument();
        m_statusMessage = "Saved.";
        UpdateWindowTitle();
        return true;
    }
    m_statusMessage = "Error: could not save file";
    return false;
}

// ---------------------------------------------------------------------------
// Menu system
// ---------------------------------------------------------------------------

void Application::OpenMenuBar()
{
    m_promptMode = PromptMode::MenuBar;
    m_activeMenu = 0;
    m_activeItem = -1;
}

void Application::OpenMenu(int menuIdx)
{
    const auto& menus = GetMenuDefs();
    const int   n     = static_cast<int>(menus.size());
    if (menuIdx < 0)   menuIdx = n - 1;
    if (menuIdx >= n)  menuIdx = 0;
    m_promptMode = PromptMode::MenuOpen;
    m_activeMenu = menuIdx;
    m_activeItem = FirstSelectableItem(menuIdx);
}

void Application::CloseMenu()
{
    m_promptMode = PromptMode::None;
    m_activeMenu = -1;
    m_activeItem = -1;
}

int Application::FirstSelectableItem(int menuIdx) const
{
    const auto& items = GetMenuDefs()[menuIdx].items;
    for (int i = 0; i < static_cast<int>(items.size()); ++i)
        if (!items[i].label.empty())
            return i;
    return -1;
}

int Application::NextSelectableItem(int menuIdx, int fromItem, int dir) const
{
    const auto& items = GetMenuDefs()[menuIdx].items;
    const int   n     = static_cast<int>(items.size());
    int i = fromItem + dir;
    while (i >= 0 && i < n)
    {
        if (!items[i].label.empty()) return i;
        i += dir;
    }
    return fromItem; // stay put if no other selectable item exists
}

void Application::ExecuteMenuItem(int menuIdx, int itemIdx)
{
    CloseMenu();

    const auto& menus = GetMenuDefs();
    if (menuIdx < 0 || menuIdx >= static_cast<int>(menus.size())) return;
    const auto& items = menus[menuIdx].items;
    if (itemIdx < 0 || itemIdx >= static_cast<int>(items.size())) return;
    if (items[itemIdx].label.empty()) return; // separator

    switch (menuIdx)
    {
        case 0: // File
            switch (itemIdx)
            {
                case 0: // New
                    if (m_document->IsDirty())
                        m_promptMode = PromptMode::ConfirmNew;
                    else
                        NewFile();
                    break;
                case 1: StartOpenPrompt();  break;  // Open...
                case 2: SaveDocument();     break;  // Save
                case 3: StartSaveAsPrompt(); break; // Save As...
                case 4: OpenPrintDialog();  break;  // Print...
                case 6: // Exit
                    RequestExit();
                    break;
                default: break;
            }
            break;

        case 1: // Edit
            switch (itemIdx)
            {
                case 0: DoUndo();        break;
                case 1: DoRedo();        break;
                case 3: CutSelection();  break;
                case 4: CopySelection(); break;
                case 5: PasteClipboard(); break;
                case 7: // Select All
                {
                    int lastLine = m_document->Buffer().LineCount() - 1;
                    m_selection.anchorRow = 0;
                    m_selection.anchorCol = 0;
                    m_selection.active    = true;
                    m_cursor.row    = lastLine;
                    m_cursor.column = m_document->Buffer().LineLength(lastLine);
                    ScrollViewport();
                    m_lastActionWasInsert = false;
                    break;
                }
                case 8: StartFindPrompt(); break;
                default: break;
            }
            break;

        case 2: // Format
            switch (itemIdx)
            {
                case 0: ToggleBold();          break;
                case 1: ToggleItalic();        break;
                case 2: ToggleUnderline();     break;
                case 3: ToggleStrikethrough(); break;
                case 4: OpenColorDialog();     break;
                case 5: OpenHighlightDialog(); break;
                case 6: InsertPageBreak();     break;
                default: break;
            }
            break;

        case 3: // Search
            switch (itemIdx)
            {
                case 0: StartFindPrompt(); break;
                case 1: FindNext();        break;
                default: break;
            }
            break;

        case 5: // Page
            switch (itemIdx)
            {
                case 0: OpenMarginsDialog(); break; // Margins...
                default: break;
            }
            break;

        case 6: // Tools
            switch (itemIdx)
            {
                case 0: OpenAddWordDialog();    break;
                case 1: OpenRemoveWordDialog(); break;
                case 3: OpenCheckWordDialog();  break;
                default: break;
            }
            break;

        case 7: // Options
            switch (itemIdx)
            {
                case 0: OpenFontDialog();  break;   // Font...
                case 1: OpenThemeDialog(); break;   // Theme...
                case 2: OpenWordWrapDialog(); break; // Word Wrap
                case 3: OpenWordCountDialog(); break; // Word Count
                case 4: ToggleSpellCheck(); break;
                case 5: ToggleHighlightMisspelled(); break;
                default: break;
            }
            break;

        case 8: // Help
            switch (itemIdx)
            {
                case 0: m_promptMode = PromptMode::HelpScreen;  break;
                case 2: m_promptMode = PromptMode::AboutScreen; break;
                default: break;
            }
            break;

        default:
            m_statusMessage = "Not implemented";
            break;
    }
}

// ---------------------------------------------------------------------------
// Cursor / viewport helpers
// ---------------------------------------------------------------------------

void Application::ClampCursorToLine()
{
    int lineCount = m_document->Buffer().LineCount();
    m_cursor.row    = std::clamp(m_cursor.row, 0, std::max(0, lineCount - 1));
    int lineLen     = m_document->Buffer().LineLength(m_cursor.row);
    m_cursor.column = std::clamp(m_cursor.column, 0, lineLen);
}

int Application::DisplayRowsForLine(int bufRow) const
{
    if (bufRow < 0 || bufRow >= m_document->Buffer().LineCount()) return 1;
    return CountWrapRows(m_document->Buffer().Line(bufRow), m_screenColumns);
}

void Application::ScrollViewport()
{
    if (m_wordWrap)
    {
        // Wrap mode — measure scroll in display rows, no horizontal scroll.
        m_viewportLeft = 0;

        if (m_cursor.row < m_viewportTop)
            m_viewportTop = m_cursor.row;

        // Display row of the cursor relative to viewportTop.
        auto cursorDisplayRow = [&]() {
            int sum = 0;
            for (int r = m_viewportTop; r < m_cursor.row; ++r)
                sum += DisplayRowsForLine(r);
            auto starts = ComputeWrapStarts(
                m_document->Buffer().Line(m_cursor.row), m_screenColumns);
            sum += WrapSegmentForColumn(starts, m_cursor.column);
            return sum;
        };

        // If cursor is below the visible area, advance viewportTop one
        // buffer line at a time until it fits (or until viewportTop catches up).
        while (cursorDisplayRow() >= m_layout.EDITOR_ROWS
               && m_viewportTop < m_cursor.row)
        {
            ++m_viewportTop;
        }
        return;
    }

    // No-wrap: vertical + horizontal scroll
    int cursorRow = m_cursor.row - m_viewportTop;
    if (cursorRow < 0)
        m_viewportTop = m_cursor.row;
    else if (cursorRow >= m_layout.EDITOR_ROWS)
        m_viewportTop = m_cursor.row - m_layout.EDITOR_ROWS + 1;

    int cursorCol = m_cursor.column - m_viewportLeft;
    if (cursorCol < 0)
        m_viewportLeft = m_cursor.column;
    else if (cursorCol >= m_screenColumns)
        m_viewportLeft = m_cursor.column - m_screenColumns + 1;
}

void Application::OpenFontDialog()
{
    m_promptMode            = PromptMode::FontDialog;
    // Seed precedence:
    //   1. First char of the active selection (so reopening the dialog
    //      after pinning a face/size reflects what's actually selected).
    //   2. m_currentFace / m_currentSize (the next-typed face/size set
    //      by a previous no-selection dialog commit).
    //   3. Document default (m_fontSettings) when neither is set.
    FontFace seedFace = m_fontSettings.face;
    FontSize seedSize = m_fontSettings.size;
    if (m_currentFace != CharFormat::Inherit
        && m_currentFace < static_cast<uint8_t>(FontFace::Count_))
        seedFace = static_cast<FontFace>(m_currentFace);
    if (m_currentSize != CharFormat::Inherit && m_currentSize < 4)
        seedSize = FontSizeAt(static_cast<int>(m_currentSize));
    if (m_selection.active && !m_selection.IsEmpty(m_cursor.row, m_cursor.column))
    {
        int sr, sc, er, ec;
        m_selection.GetRange(m_cursor.row, m_cursor.column, sr, sc, er, ec);
        CharFormat f = m_document->Buffer().FormatAt(sr, sc);
        if (f.face != CharFormat::Inherit && f.face < static_cast<uint8_t>(FontFace::Count_))
            seedFace = static_cast<FontFace>(f.face);
        if (f.size != CharFormat::Inherit && f.size < 4)
            seedSize = FontSizeAt(static_cast<int>(f.size));
    }
    int presetIdx = static_cast<int>(seedFace) * FontSizeCount()
                  + IndexOfFontSize(seedSize);
    int presetCount = FontFaceCount() * FontSizeCount();
    m_fontDialogPresetIdx = std::clamp(presetIdx, 0, std::max(0, presetCount - 1));
    // Center the seed in the viewport when possible.
    constexpr int visibleRows = 12;
    int maxScroll = std::max(0, presetCount - visibleRows);
    m_fontDialogScrollTop = std::clamp(
        m_fontDialogPresetIdx - visibleRows / 2, 0, maxScroll);
}

void Application::ApplyFontDialogSelection()
{
    FontFace face = static_cast<FontFace>(m_fontDialogPresetIdx / FontSizeCount());
    FontSize size = FontSizeAt(m_fontDialogPresetIdx % FontSizeCount());
    m_promptMode = PromptMode::None;

    // Always update next-typed face/size so subsequent typing matches what
    // the user just picked — even after pinning the same value to a
    // selection.
    m_currentFace = static_cast<uint8_t>(face);
    m_currentSize = static_cast<uint8_t>(IndexOfFontSize(size));

    if (m_selection.active && !m_selection.IsEmpty(m_cursor.row, m_cursor.column))
    {
        // With a selection active, pin face+size to the selected range.
        int sr, sc, er, ec;
        m_selection.GetRange(m_cursor.row, m_cursor.column, sr, sc, er, ec);
        PushUndoBeforeEdit();
        m_document->Buffer().SetFaceInRange(sr, sc, er, ec,
            static_cast<uint8_t>(face));
        m_document->Buffer().SetSizeInRange(sr, sc, er, ec,
            static_cast<uint8_t>(IndexOfFontSize(size)));
        m_document->MarkDirty();
        UpdateWindowTitle();

        // If the selection covered the entire document (Ctrl+A then pick),
        // also update the document default so the saved RTF carries the
        // new face/size in `\deff` / `\fs` and reopens with that as the
        // baseline — without this, every body char would emit an
        // explicit `\fN/\fsN` override and a fresh insert at end-of-doc
        // would inherit the old default instead of matching surrounding
        // text.
        const auto& tb = m_document->Buffer().Text();
        int lastRow = tb.LineCount() - 1;
        bool selCoversAll = (sr == 0 && sc == 0 && er == lastRow
                             && ec == static_cast<int>(tb.LineLength(lastRow)));
        if (selCoversAll && !(FontSettings{ face, size } == m_fontSettings))
            ApplyFontSettings(FontSettings{ face, size });

        m_statusMessage = "Font applied to selection";
        return;
    }

    // No selection — the picked face/size affects only next-typed input
    // (via the m_currentFace / m_currentSize fields set above). The
    // document default (m_fontSettings) is left untouched so existing
    // Inherit-face/size characters are not retroactively restyled. To
    // restyle every character in the document, the user can press
    // Ctrl+A (Select All) before opening the Font dialog.
    m_statusMessage = std::string("Font (next-typed): ") + FontFaceName(face);
}

void Application::OpenThemeDialog()
{
    m_promptMode          = PromptMode::ThemeDialog;
    m_themeDialogFocusIdx = static_cast<int>(m_themeName);
    m_statusMessage.clear();
}

void Application::ApplyThemeDialogSelection()
{
    int idx = std::clamp(m_themeDialogFocusIdx, 0, ThemeCount() - 1);
    ThemeName chosen = static_cast<ThemeName>(idx);
    m_promptMode = PromptMode::None;
    if (chosen == m_themeName)
    {
        m_statusMessage = "Ready";
        return;
    }
    ApplyTheme(chosen);
    m_statusMessage = std::string("Theme: ") + ThemeDisplayName(chosen);
}

void Application::OpenColorDialog()
{
    m_promptMode = PromptMode::ColorDialog;
    // Seed the dialog from the first char of the selection if active, else
    // from m_currentColor. Inherit means "no override" — show the cursor at
    // the first swatch (Black) as a sensible default focus.
    uint8_t seed = m_currentColor;
    if (m_selection.active && !m_selection.IsEmpty(m_cursor.row, m_cursor.column))
    {
        int sr, sc, er, ec;
        m_selection.GetRange(m_cursor.row, m_cursor.column, sr, sc, er, ec);
        uint8_t c = m_document->Buffer().FormatAt(sr, sc).color;
        if (c != CharFormat::Inherit) seed = c;
    }
    if (seed == CharFormat::Inherit) seed = 0;
    m_colorDialogFocusIdx = std::clamp(static_cast<int>(seed), 0, Palette::kCount - 1);
    m_statusMessage.clear();
}

void Application::ApplyColorDialogSelection()
{
    int idx = std::clamp(m_colorDialogFocusIdx, 0, Palette::kCount - 1);
    m_promptMode = PromptMode::None;

    if (m_selection.active && !m_selection.IsEmpty(m_cursor.row, m_cursor.column))
    {
        int sr, sc, er, ec;
        m_selection.GetRange(m_cursor.row, m_cursor.column, sr, sc, er, ec);
        PushUndoBeforeEdit();
        m_document->Buffer().SetColorInRange(sr, sc, er, ec, static_cast<uint8_t>(idx));
        m_document->MarkDirty();
        UpdateWindowTitle();
        m_statusMessage = std::string("Color: ") + Palette::NameAt(static_cast<uint8_t>(idx));
        return;
    }

    m_currentColor = static_cast<uint8_t>(idx);
    m_statusMessage = std::string("Color (next-typed): ") + Palette::NameAt(static_cast<uint8_t>(idx));
}

void Application::OpenHighlightDialog()
{
    m_promptMode = PromptMode::HighlightDialog;
    uint8_t seed = m_currentHighlight;
    if (m_selection.active && !m_selection.IsEmpty(m_cursor.row, m_cursor.column))
    {
        int sr, sc, er, ec;
        m_selection.GetRange(m_cursor.row, m_cursor.column, sr, sc, er, ec);
        uint8_t h = m_document->Buffer().FormatAt(sr, sc).highlight;
        if (h != CharFormat::Inherit) seed = h;
    }
    if (seed == CharFormat::Inherit) seed = 11; // Yellow — the conventional default
    m_colorDialogFocusIdx = std::clamp(static_cast<int>(seed), 0, Palette::kCount - 1);
    m_statusMessage.clear();
}

void Application::ApplyHighlightDialogSelection()
{
    int idx = std::clamp(m_colorDialogFocusIdx, 0, Palette::kCount - 1);
    m_promptMode = PromptMode::None;

    if (m_selection.active && !m_selection.IsEmpty(m_cursor.row, m_cursor.column))
    {
        int sr, sc, er, ec;
        m_selection.GetRange(m_cursor.row, m_cursor.column, sr, sc, er, ec);
        PushUndoBeforeEdit();
        m_document->Buffer().SetHighlightInRange(sr, sc, er, ec, static_cast<uint8_t>(idx));
        m_document->MarkDirty();
        UpdateWindowTitle();
        m_statusMessage = std::string("Highlight: ") + Palette::NameAt(static_cast<uint8_t>(idx));
        return;
    }

    m_currentHighlight = static_cast<uint8_t>(idx);
    m_statusMessage = std::string("Highlight (next-typed): ") + Palette::NameAt(static_cast<uint8_t>(idx));
}

void Application::InsertPageBreak()
{
    // Ctrl+Enter / Format > Insert Page Break: split the current line at
    // the cursor and set the new line's page-break-before flag. The
    // pagination loop in WysiwygRenderer / Print respects the flag and
    // starts that paragraph on a fresh page.
    PushUndoBeforeEdit();
    if (m_selection.active && !m_selection.IsEmpty(m_cursor.row, m_cursor.column))
        EraseSelection();
    m_document->Buffer().InsertNewline(m_cursor.column, m_cursor.row);
    m_document->Buffer().SetPageBreakBefore(m_cursor.row + 1, true);
    m_cursor.row    += 1;
    m_cursor.column  = 0;
    m_selection.Clear();
    m_document->MarkDirty();
    ScrollViewport();
    UpdateWindowTitle();
    m_statusMessage = "Page break inserted";
}

// Shared by the keyboard Y/N handlers and the mouse Yes/No clicks on confirm
// dialogs. Reads m_promptMode to know which confirm is active, then resets it.
void Application::ResolveConfirmYes()
{
    PromptMode mode = m_promptMode;
    m_promptMode = PromptMode::None;
    switch (mode)
    {
        case PromptMode::ConfirmExit:
            // Save and exit. If no filename, chain into Save As dialog and
            // exit on its successful commit.
            if (m_document->Filename().empty())
            {
                m_exitAfterSave = true;
                StartSaveAsPrompt();
            }
            else if (m_document->Save(m_fontSettings.face,
                                      FontSizePoints(m_fontSettings.size),
                                      CurrentRtfPage()))
            {
                WriteSidecarForCurrentDocument();
                UpdateWindowTitle();
                m_running = false;
            }
            else
            {
                m_statusMessage = "Error: could not save file";
            }
            break;
        case PromptMode::ConfirmExitClean:
            m_running = false;
            break;
        case PromptMode::ConfirmNew:
            NewFile();
            break;
        case PromptMode::ConfirmWordWrap:
            SetWordWrap(true);
            WriteSidecarForCurrentDocument();
            break;
        case PromptMode::ConfirmSaveAsRtf:
        {
            // Y → Save As .rtf. Prefill with the current basename + .rtf
            // so the user can just hit Enter to accept the new path.
            std::string prefill = m_document->Filename();
            auto dot = prefill.find_last_of('.');
            if (dot != std::string::npos) prefill.resize(dot);
            prefill += ".rtf";
            m_promptText = std::move(prefill);
            m_promptMode = PromptMode::SaveAs;
            m_statusMessage.clear();
            break;
        }
        default:
            break;
    }
}

void Application::ResolveConfirmNo()
{
    PromptMode mode = m_promptMode;
    m_promptMode = PromptMode::None;
    switch (mode)
    {
        case PromptMode::ConfirmExit:
            m_running = false; // discard and exit
            break;
        case PromptMode::ConfirmExitClean:
        case PromptMode::ConfirmNew:
            m_statusMessage = "Ready";
            break;
        case PromptMode::ConfirmWordWrap:
            SetWordWrap(false);
            WriteSidecarForCurrentDocument();
            break;
        case PromptMode::ConfirmSaveAsRtf:
            // N → drop formatting and save as the original .txt path. The
            // user accepted the warning; flatten the in-memory styles so
            // the next save round-trips correctly.
            m_document->Buffer().FlattenAllStyles();
            if (m_document->Save(m_fontSettings.face,
                                 FontSizePoints(m_fontSettings.size),
                                 CurrentRtfPage()))
            {
                WriteSidecarForCurrentDocument();
                m_statusMessage = "Saved as plain text (formatting discarded).";
                UpdateWindowTitle();
            }
            else
            {
                m_statusMessage = "Error: could not save file";
            }
            break;
        default:
            break;
    }
}

void Application::OpenWordWrapDialog()
{
    m_promptMode = PromptMode::ConfirmWordWrap;
}

void Application::SetWordWrap(bool on)
{
    if (m_wordWrap == on)
    {
        m_statusMessage = on ? "Word wrap is already on" : "Word wrap is already off";
        return;
    }
    m_wordWrap = on;
    if (m_wordWrap)
        m_viewportLeft = 0;
    ScrollViewport();
    m_statusMessage = m_wordWrap ? "Word wrap on" : "Word wrap off";
}

void Application::OpenWordCountDialog()
{
    m_promptMode = PromptMode::WordCountDialog;
}

void Application::ToggleStatusBarWordCount()
{
    m_showWordCount = !m_showWordCount;
    WriteSidecarForCurrentDocument();
}

// ---------------------------------------------------------------------------
// Spell check
// ---------------------------------------------------------------------------

void Application::OpenAddWordDialog()
{
    m_promptMode = PromptMode::AddWordDialog;
    m_promptText.clear();
    m_statusMessage.clear();
}

void Application::OpenRemoveWordDialog()
{
    m_promptMode = PromptMode::RemoveWordDialog;
    m_promptText.clear();
    m_statusMessage.clear();
}

void Application::OpenCheckWordDialog()
{
    m_promptMode = PromptMode::CheckWordDialog;
    m_promptText.clear();
    m_statusMessage.clear();
}

void Application::ToggleSpellCheck()
{
    m_spellCheckEnabled = !m_spellCheckEnabled;
    m_statusMessage = m_spellCheckEnabled ? "Spell check on" : "Spell check off";
    SaveGlobalSettings();
}

void Application::ToggleHighlightMisspelled()
{
    m_highlightMisspelled = !m_highlightMisspelled;
    m_statusMessage = m_highlightMisspelled
        ? "Misspelled highlighting on"
        : "Misspelled highlighting off";
    SaveGlobalSettings();
}

void Application::CheckJustCompletedWord()
{
    // The cursor sits just after the boundary character; the word ended at
    // cursor.column - 2 (one back for the boundary char itself).
    const std::string& line = m_document->Buffer().Line(m_cursor.row);
    int end = m_cursor.column - 2;
    if (end < 0 || end >= static_cast<int>(line.size())) return;

    auto isLetter = [](char c) {
        unsigned char uc = static_cast<unsigned char>(c);
        return std::isalpha(uc) != 0 || c == '\'';
    };

    // No word just completed if the character before the boundary isn't
    // itself a letter (e.g. typing space after space, or at line start).
    if (!isLetter(line[end])) return;

    int start = end;
    while (start > 0 && isLetter(line[start - 1]))
        --start;

    int wordEnd = end + 1; // exclusive
    // Strip leading/trailing apostrophes.
    while (start < wordEnd && line[start]      == '\'') ++start;
    while (wordEnd > start && line[wordEnd-1]  == '\'') --wordEnd;

    if (wordEnd <= start) return;

    std::string word = line.substr(static_cast<size_t>(start),
                                   static_cast<size_t>(wordEnd - start));
    if (!m_dictionary.Contains(word))
        PlayBeep();
}

void Application::SaveUserDictionary()
{
    std::string dir = UserConfigDir();
    if (dir.empty()) return;
    m_dictionary.SaveUserOverlay(dir + "user_dictionary.txt");
}

void Application::LoadGlobalSettings()
{
    std::string dir = UserConfigDir();
    if (dir.empty()) return;

    FileSettings s;
    if (s.Load(dir + "config.ini"))
    {
        if (s.Has("spell_check"))
            m_spellCheckEnabled = s.GetBool("spell_check");
        if (s.Has("highlight_misspelled"))
            m_highlightMisspelled = s.GetBool("highlight_misspelled");
        if (s.Has("theme_name"))
            m_themeName = ParseThemeName(s.GetString("theme_name"));
    }
    m_theme = MakeTheme(m_themeName);

    m_dictionary.LoadUserOverlay(dir + "user_dictionary.txt");
}

void Application::SaveGlobalSettings()
{
    std::string dir = UserConfigDir();
    if (dir.empty()) return;

    FileSettings s;
    // Load first so any future keys round-trip through older code paths.
    s.Load(dir + "config.ini");
    s.SetBool("spell_check",          m_spellCheckEnabled);
    s.SetBool("highlight_misspelled", m_highlightMisspelled);
    s.SetString("theme_name",         ThemeNameKey(m_themeName));
    s.Save(dir + "config.ini");
}

void Application::ApplyTheme(ThemeName name)
{
    if (name == m_themeName) return;
    m_themeName = name;
    m_theme = MakeTheme(name);
    m_needsRedraw = true;
    SaveGlobalSettings();
}

// ---------------------------------------------------------------------------
// Print
// ---------------------------------------------------------------------------

namespace
{
    std::string FormatMarginText(double v)
    {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.2f", v);
        return buf;
    }

    bool IsNumericField(PrintField f)
    {
        switch (f)
        {
            case PrintField::Copies:
            case PrintField::RangeFrom:
            case PrintField::RangeTo:
            case PrintField::MarginTop:
            case PrintField::MarginBottom:
            case PrintField::MarginLeft:
            case PrintField::MarginRight:
                return true;
            default:
                return false;
        }
    }

    bool IsMarginField(PrintField f)
    {
        switch (f)
        {
            case PrintField::MarginTop:
            case PrintField::MarginBottom:
            case PrintField::MarginLeft:
            case PrintField::MarginRight:
                return true;
            default:
                return false;
        }
    }

    int MarginIndex(PrintField f)
    {
        switch (f)
        {
            case PrintField::MarginTop:    return 0;
            case PrintField::MarginBottom: return 1;
            case PrintField::MarginLeft:   return 2;
            case PrintField::MarginRight:  return 3;
            default:                       return -1;
        }
    }

    int ParseIntOr(const std::string& s, int def)
    {
        if (s.empty()) return def;
        try { return std::stoi(s); } catch (...) { return def; }
    }

    double ParseDoubleOr(const std::string& s, double def)
    {
        if (s.empty()) return def;
        try { return std::stod(s); } catch (...) { return def; }
    }
}

void Application::OpenPrintDialog()
{
    m_printerList = EnumeratePrinters();
    if (m_printerList.empty())
    {
        m_statusMessage = "No printers installed.";
        return;
    }

    // Find current printer in the list, default to 0.
    m_printPrinterIdx = 0;
    if (!m_printRequest.printerName.empty())
    {
        for (int i = 0; i < static_cast<int>(m_printerList.size()); ++i)
            if (m_printerList[i] == m_printRequest.printerName) { m_printPrinterIdx = i; break; }
    }

    m_printCopiesText      = std::to_string(std::max(1, m_printRequest.copies));
    m_printFromText        = std::to_string(std::max(1, m_printRequest.pageFrom));
    m_printToText          = std::to_string(std::max(1, m_printRequest.pageTo));
    // Seed the print dialog's margins from the document so "Print" out of
    // the box matches what's on screen.
    PrintMargins defaultMargins = m_printRequest.margins;
    defaultMargins.topIn    = m_margins.topIn;
    defaultMargins.bottomIn = m_margins.bottomIn;
    defaultMargins.leftIn   = m_margins.leftIn;
    defaultMargins.rightIn  = m_margins.rightIn;
    m_printMarginText[0]   = FormatMarginText(defaultMargins.topIn);
    m_printMarginText[1]   = FormatMarginText(defaultMargins.bottomIn);
    m_printMarginText[2]   = FormatMarginText(defaultMargins.leftIn);
    m_printMarginText[3]   = FormatMarginText(defaultMargins.rightIn);
    m_printFocus           = PrintField::Printer;

    m_promptMode    = PromptMode::PrintDialog;
    m_statusMessage.clear();
}

void Application::ClosePrintDialog(bool commit)
{
    if (!commit)
    {
        m_promptMode    = PromptMode::None;
        m_statusMessage = "Ready";
        return;
    }

    // Parse and clamp edit strings into the request.
    int copies = std::clamp(ParseIntOr(m_printCopiesText, 1), 1, 99);
    int from   = std::max(1, ParseIntOr(m_printFromText, 1));
    int to     = std::max(from, ParseIntOr(m_printToText, from));
    auto clampMargin = [](double v) { return std::clamp(v, 0.0, 5.0); };

    m_printRequest.copies      = copies;
    m_printRequest.pageFrom    = from;
    m_printRequest.pageTo      = to;
    m_printRequest.margins.topIn    = clampMargin(ParseDoubleOr(m_printMarginText[0], 0.5));
    m_printRequest.margins.bottomIn = clampMargin(ParseDoubleOr(m_printMarginText[1], 0.5));
    m_printRequest.margins.leftIn   = clampMargin(ParseDoubleOr(m_printMarginText[2], 0.75));
    m_printRequest.margins.rightIn  = clampMargin(ParseDoubleOr(m_printMarginText[3], 0.75));
    m_printRequest.printerName  = m_printerList[m_printPrinterIdx];
    m_printRequest.documentName = m_document->DisplayName();

    // Print uses the editor's current font/size so the output matches what
    // is on screen. Bundled TTFs are registered privately by PrintDocument
    // via AddFontResourceEx so users don't need the font installed
    // system-wide. The formatted print path uses pixel-based wrap with
    // per-char advance — exactly mirrors the screen renderer.
    m_printRequest.useDocumentFont = true;
    m_printRequest.fontFamily      = FontFaceFamily(m_fontSettings.face);
    m_printRequest.fontFile        = ResolveAssetPath(FontFaceFile(m_fontSettings.face));
    m_printRequest.pointSize       = FontSizePoints(m_fontSettings.size);
    m_printRequest.bold            = FontFaceIsBold(m_fontSettings.face);
    m_printRequest.formats         = &m_document->Buffer().Formats();
    m_printRequest.pageBreakBefore = &m_document->Buffer().PageBreaks();

    m_promptMode    = PromptMode::None;
    m_statusMessage = "Printing...";
    // Repaint so the user sees "Printing..." while the synchronous job runs.
    Render();

    m_statusMessage = PrintDocument(m_document->Buffer().Text(), m_printRequest);
}

void Application::PrintCycleField(int dir)
{
    int idx = static_cast<int>(m_printFocus);
    int n   = static_cast<int>(PrintField::Count);
    idx = ((idx + dir) % n + n) % n;
    m_printFocus = static_cast<PrintField>(idx);
}

void Application::PrintAdjustField(int dir)
{
    auto bumpInt = [&](std::string& s, int lo, int hi) {
        int v = std::clamp(ParseIntOr(s, lo) + dir, lo, hi);
        s = std::to_string(v);
    };
    auto bumpMargin = [&](std::string& s) {
        double v = ParseDoubleOr(s, 0.5) + 0.05 * dir;
        v = std::clamp(v, 0.0, 5.0);
        s = FormatMarginText(v);
    };

    switch (m_printFocus)
    {
        case PrintField::Printer:
        {
            int n = static_cast<int>(m_printerList.size());
            if (n > 0) m_printPrinterIdx = ((m_printPrinterIdx + dir) % n + n) % n;
            break;
        }
        case PrintField::Copies:      bumpInt(m_printCopiesText, 1, 99); break;
        case PrintField::RangeMode:
            m_printRequest.allPages = !m_printRequest.allPages;
            break;
        case PrintField::RangeFrom:   bumpInt(m_printFromText, 1, 9999); break;
        case PrintField::RangeTo:     bumpInt(m_printToText,   1, 9999); break;
        case PrintField::Orientation:
            m_printRequest.orientation =
                (m_printRequest.orientation == PrintOrientation::Portrait)
                    ? PrintOrientation::Landscape : PrintOrientation::Portrait;
            break;
        case PrintField::MarginTop:    bumpMargin(m_printMarginText[0]); break;
        case PrintField::MarginBottom: bumpMargin(m_printMarginText[1]); break;
        case PrintField::MarginLeft:   bumpMargin(m_printMarginText[2]); break;
        case PrintField::MarginRight:  bumpMargin(m_printMarginText[3]); break;
        default: break;
    }
}

void Application::PrintTextEdit(char ch)
{
    if (!IsNumericField(m_printFocus)) return;

    bool isDigit = (ch >= '0' && ch <= '9');
    bool isDot   = (ch == '.');
    if (!isDigit && !(isDot && IsMarginField(m_printFocus))) return;

    std::string* target = nullptr;
    int          maxLen = 4;
    switch (m_printFocus)
    {
        case PrintField::Copies:       target = &m_printCopiesText;    maxLen = 2; break;
        case PrintField::RangeFrom:    target = &m_printFromText;      maxLen = 4; break;
        case PrintField::RangeTo:      target = &m_printToText;        maxLen = 4; break;
        case PrintField::MarginTop:    target = &m_printMarginText[0]; maxLen = 5; break;
        case PrintField::MarginBottom: target = &m_printMarginText[1]; maxLen = 5; break;
        case PrintField::MarginLeft:   target = &m_printMarginText[2]; maxLen = 5; break;
        case PrintField::MarginRight:  target = &m_printMarginText[3]; maxLen = 5; break;
        default: return;
    }
    if (!target) return;
    if (isDot && target->find('.') != std::string::npos) return; // one dot max
    if (static_cast<int>(target->size()) >= maxLen) return;
    target->push_back(ch);
}

void Application::PrintBackspace()
{
    std::string* target = nullptr;
    switch (m_printFocus)
    {
        case PrintField::Copies:       target = &m_printCopiesText;    break;
        case PrintField::RangeFrom:    target = &m_printFromText;      break;
        case PrintField::RangeTo:      target = &m_printToText;        break;
        case PrintField::MarginTop:    target = &m_printMarginText[0]; break;
        case PrintField::MarginBottom: target = &m_printMarginText[1]; break;
        case PrintField::MarginLeft:   target = &m_printMarginText[2]; break;
        case PrintField::MarginRight:  target = &m_printMarginText[3]; break;
        default: break;
    }
    if (target && !target->empty()) target->pop_back();
}

// ---------------------------------------------------------------------------
// Per-character formatting (Format menu / Ctrl+B / Ctrl+I / Ctrl+U)
// ---------------------------------------------------------------------------

void Application::ApplyStyleAction(uint8_t bit)
{
    if (bit == 0) return;

    if (m_selection.active && !m_selection.IsEmpty(m_cursor.row, m_cursor.column))
    {
        int sr, sc, er, ec;
        m_selection.GetRange(m_cursor.row, m_cursor.column, sr, sc, er, ec);
        PushUndoBeforeEdit();
        bool allOn = m_document->Buffer().AllInRangeHaveStyle(sr, sc, er, ec, bit);
        m_document->Buffer().SetStyleInRange(sr, sc, er, ec, bit, !allOn);
        m_document->MarkDirty();
        UpdateWindowTitle();
    }
    else
    {
        // No selection: toggle the bit for next-typed input. No undo entry —
        // this changes editor intent, not document content.
        m_currentStyle ^= bit;
    }
    m_lastActionWasInsert = false;
}

void Application::ToggleBold()          { ApplyStyleAction(CharStyle::Bold); }
void Application::ToggleItalic()        { ApplyStyleAction(CharStyle::Italic); }
void Application::ToggleUnderline()     { ApplyStyleAction(CharStyle::Underline); }
void Application::ToggleStrikethrough() { ApplyStyleAction(CharStyle::Strikethrough); }

// ---------------------------------------------------------------------------
// Margins dialog
// ---------------------------------------------------------------------------

void Application::OpenMarginsDialog()
{
    char buf[16];
    auto fmt = [&](double v) {
        std::snprintf(buf, sizeof(buf), "%.2f", v);
        return std::string(buf);
    };
    m_marginEditText[0] = fmt(m_margins.topIn);
    m_marginEditText[1] = fmt(m_margins.bottomIn);
    m_marginEditText[2] = fmt(m_margins.leftIn);
    m_marginEditText[3] = fmt(m_margins.rightIn);
    m_marginFocusIdx    = 0;
    m_promptMode        = PromptMode::MarginsDialog;
    m_statusMessage.clear();
}

void Application::CloseMarginsDialog(bool commit)
{
    if (!commit)
    {
        m_promptMode    = PromptMode::None;
        m_statusMessage = "Ready";
        return;
    }
    auto parse = [&](const std::string& s, double def) {
        if (s.empty()) return def;
        try { return std::stod(s); } catch (...) { return def; }
    };
    auto clamp = [](double v) { return std::clamp(v, 0.0, 5.0); };

    m_margins.topIn    = clamp(parse(m_marginEditText[0], m_margins.topIn));
    m_margins.bottomIn = clamp(parse(m_marginEditText[1], m_margins.bottomIn));
    m_margins.leftIn   = clamp(parse(m_marginEditText[2], m_margins.leftIn));
    m_margins.rightIn  = clamp(parse(m_marginEditText[3], m_margins.rightIn));

    m_promptMode    = PromptMode::None;
    m_statusMessage = "Margins updated";
    WriteSidecarForCurrentDocument();
}

void Application::MarginCycleField(int dir)
{
    m_marginFocusIdx = ((m_marginFocusIdx + dir) % 4 + 4) % 4;
}

void Application::MarginAdjustField(int dir)
{
    if (m_marginFocusIdx < 0 || m_marginFocusIdx > 3) return;
    auto parse = [&](const std::string& s) -> double {
        if (s.empty()) return 0.5;
        try { return std::stod(s); } catch (...) { return 0.5; }
    };
    double v = parse(m_marginEditText[m_marginFocusIdx]) + 0.05 * dir;
    v = std::clamp(v, 0.0, 5.0);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.2f", v);
    m_marginEditText[m_marginFocusIdx] = buf;
}

void Application::MarginTextEdit(char ch)
{
    if (m_marginFocusIdx < 0 || m_marginFocusIdx > 3) return;
    bool isDigit = (ch >= '0' && ch <= '9');
    bool isDot   = (ch == '.');
    if (!isDigit && !isDot) return;
    std::string& s = m_marginEditText[m_marginFocusIdx];
    if (isDot && s.find('.') != std::string::npos) return;
    if (s.size() >= 5) return;
    s.push_back(ch);
}

void Application::MarginBackspace()
{
    if (m_marginFocusIdx < 0 || m_marginFocusIdx > 3) return;
    std::string& s = m_marginEditText[m_marginFocusIdx];
    if (!s.empty()) s.pop_back();
}

// ---------------------------------------------------------------------------
// Per-file settings sidecar
//
// To add a new persisted setting:
//   - write it in CaptureFileSettings (one line)
//   - read it in ApplyFileSettings  (one Has-guarded line)
// The Load/Save plumbing below is generic and needs no further changes.
// ---------------------------------------------------------------------------

void Application::CaptureFileSettings(FileSettings& s) const
{
    s.SetBool("word_wrap",       m_wordWrap);
    s.SetBool("show_word_count", m_showWordCount);
    s.SetString("margin_top",    std::to_string(m_margins.topIn));
    s.SetString("margin_bottom", std::to_string(m_margins.bottomIn));
    s.SetString("margin_left",   std::to_string(m_margins.leftIn));
    s.SetString("margin_right",  std::to_string(m_margins.rightIn));
}

void Application::ApplyFileSettings(const FileSettings& s)
{
    if (s.Has("word_wrap"))
        SetWordWrap(s.GetBool("word_wrap"));
    if (s.Has("show_word_count"))
        m_showWordCount = s.GetBool("show_word_count");
    // Legacy `wysiwyg` sidecar key from before the product split is silently
    // ignored on read; RetroDocWriter is always WYSIWYG now.
    auto readDouble = [&](const char* key, double& out) {
        if (!s.Has(key)) return;
        try { out = std::stod(s.GetString(key)); } catch (...) {}
    };
    readDouble("margin_top",    m_margins.topIn);
    readDouble("margin_bottom", m_margins.bottomIn);
    readDouble("margin_left",   m_margins.leftIn);
    readDouble("margin_right",  m_margins.rightIn);
}

void Application::WriteSidecarForCurrentDocument()
{
    std::string sidecar = FileSettings::SidecarPath(m_document->Filename());
    if (sidecar.empty()) return;
    FileSettings s;
    CaptureFileSettings(s);
    s.Save(sidecar);  // silent on failure - sidecar is best-effort
}

void Application::LoadSidecarForCurrentDocument()
{
    std::string sidecar = FileSettings::SidecarPath(m_document->Filename());
    if (sidecar.empty()) return;
    FileSettings s;
    if (s.Load(sidecar))
        ApplyFileSettings(s);
}

void Application::HandleWindowResized(int newW, int newH)
{
    if (newW <= 0 || newH <= 0) return;
    if (newW == m_windowWidth && newH == m_windowHeight) return;

    m_windowWidth  = newW;
    m_windowHeight = newH;

    if (!m_renderer) return;
    int cw = m_renderer->CellWidth();
    int ch = m_renderer->CellHeight();
    if (cw <= 0 || ch <= 0) return;

    m_screenColumns = ComputeScreenColumns(cw);
    int rows        = ComputeScreenRows(ch);
    m_layout        = Layout(rows);

    m_screenBuffer = std::make_unique<ScreenBuffer>(m_screenColumns, rows);
    m_ui           = std::make_unique<RetroUi>(m_theme, m_layout);

    // Keep the cursor and viewport sane after the layout shifts.
    ClampCursorToLine();
    ScrollViewport();
}

void Application::ApplyFontSettings(const FontSettings& settings)
{
    m_fontSettings = settings;
    // The cell-grid chrome (menus, status bar, dialogs) renders into
    // ScreenBuffer cells of fixed width, so it must use a monospace face.
    // The document itself can be a proportional face — WysiwygRenderer
    // handles that with per-glyph advance — but RetroRenderer cannot.
    // When the document's default face is proportional, fall the chrome
    // back to the canonical monospace face at the same size so menus and
    // dialogs still render cleanly.
    FontSettings chromeSettings = settings;
    if (!FontFaceIsMonospace(chromeSettings.face))
        chromeSettings.face = FontFace::CascadiaMono;
    m_renderer->SetFontSettings(chromeSettings);
    // WysiwygRenderer rebuilds its glyph cache lazily inside Draw() when the
    // (face, point size, dpi) tuple changes — no explicit notification needed.

    // Window stays at WINDOW_WIDTH x WINDOW_HEIGHT — cell size changes, so
    // the column/row count of the screen buffer is what shifts.
    m_screenColumns = ComputeScreenColumns(m_renderer->CellWidth());
    int rows        = ComputeScreenRows(m_renderer->CellHeight());
    m_layout        = Layout(rows);

    m_screenBuffer = std::make_unique<ScreenBuffer>(m_screenColumns, rows);
    m_ui           = std::make_unique<RetroUi>(m_theme, m_layout);

    // Keep cursor visible after a layout change.
    ClampCursorToLine();
    ScrollViewport();
}

void Application::UpdateWindowTitle()
{
    std::string title = "RetroDocWriter - " + m_document->DisplayName();
    if (m_document->IsDirty())
        title += " *";
    m_window->SetTitle(title);
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

void Application::Update()
{
    UpdateCursorBlink();
}

void Application::Render()
{
    ScreenCell fill;
    fill.character  = U' ';
    fill.foreground = m_theme.normalText;
    fill.background = m_theme.background;
    m_screenBuffer->Clear(fill);

    EditorUiState uiState;
    uiState.textBuffer    = &m_document->Buffer().Text();
    uiState.viewportTop   = m_viewportTop;
    uiState.viewportLeft  = m_viewportLeft;
    uiState.filename      = m_document->DisplayName();
    uiState.dirty         = m_document->IsDirty();
    uiState.statusMessage = m_statusMessage;
    uiState.currentStyle  = m_currentStyle;

    // Modal dialog overlay for prompts and confirmations (replaces the old
    // bottom-of-screen prompt). Active only for text/confirm modes.
    uiState.dialogActive = (m_promptMode == PromptMode::Open             ||
                            m_promptMode == PromptMode::SaveAs           ||
                            m_promptMode == PromptMode::Find             ||
                            m_promptMode == PromptMode::ConfirmExit      ||
                            m_promptMode == PromptMode::ConfirmExitClean ||
                            m_promptMode == PromptMode::ConfirmNew       ||
                            m_promptMode == PromptMode::ConfirmWordWrap  ||
                            m_promptMode == PromptMode::ConfirmSaveAsRtf ||
                            m_promptMode == PromptMode::AddWordDialog    ||
                            m_promptMode == PromptMode::RemoveWordDialog ||
                            m_promptMode == PromptMode::CheckWordDialog);
    uiState.dialogIsConfirm = (m_promptMode == PromptMode::ConfirmExit         ||
                               m_promptMode == PromptMode::ConfirmExitClean    ||
                               m_promptMode == PromptMode::ConfirmNew          ||
                               m_promptMode == PromptMode::ConfirmWordWrap     ||
                               m_promptMode == PromptMode::ConfirmSaveAsRtf);
    uiState.dialogInput         = m_promptText;
    uiState.dialogCursorVisible = m_cursor.visible;
    uiState.dialogHint.clear();

    switch (m_promptMode)
    {
        case PromptMode::Open:
            uiState.dialogTitle   = "Open File";
            uiState.dialogPrompt  = "File path:";
            uiState.dialogPrompt2 = "";
            break;
        case PromptMode::SaveAs:
            uiState.dialogTitle   = "Save As";
            uiState.dialogPrompt  = "File path:";
            uiState.dialogPrompt2 = "";
            break;
        case PromptMode::Find:
            uiState.dialogTitle   = "Find";
            uiState.dialogPrompt  = "Search for:";
            uiState.dialogPrompt2 = "";
            break;
        case PromptMode::ConfirmExit:
            uiState.dialogTitle   = "Save Changes?";
            uiState.dialogPrompt  = "The document has unsaved changes.";
            uiState.dialogPrompt2 = "Save before exiting?";
            uiState.dialogHint    = "[Y] Save     [N] Discard     [Esc] Cancel";
            break;
        case PromptMode::ConfirmExitClean:
            uiState.dialogTitle   = "Confirm Exit";
            uiState.dialogPrompt  = "Are you sure you want to exit RetroDocWriter?";
            uiState.dialogPrompt2 = "";
            break;
        case PromptMode::ConfirmNew:
            uiState.dialogTitle   = "Confirm New File";
            uiState.dialogPrompt  = "Unsaved changes will be lost.";
            uiState.dialogPrompt2 = "Discard changes and start a new file?";
            break;
        case PromptMode::ConfirmWordWrap:
            uiState.dialogTitle   = "Word Wrap";
            uiState.dialogPrompt  = std::string("Word wrap is currently ")
                                  + (m_wordWrap ? "ON." : "OFF.");
            uiState.dialogPrompt2 = "Turn it on or off?";
            uiState.dialogHint    = "[Y] On      [N] Off      [Esc] Cancel";
            break;
        case PromptMode::ConfirmSaveAsRtf:
            uiState.dialogTitle   = "Save Formatted Document";
            uiState.dialogPrompt  = "This document has formatting that .txt can't hold.";
            uiState.dialogPrompt2 = "Save as .rtf to preserve it?";
            uiState.dialogHint    = "[Y] Save .rtf  [N] Save .txt  [Esc] Cancel";
            break;
        case PromptMode::AddWordDialog:
            uiState.dialogTitle   = "Add to Dictionary";
            uiState.dialogPrompt  = "Word to add:";
            uiState.dialogPrompt2 = "";
            break;
        case PromptMode::RemoveWordDialog:
            uiState.dialogTitle   = "Remove from Dictionary";
            uiState.dialogPrompt  = "Word to remove:";
            uiState.dialogPrompt2 = "";
            break;
        case PromptMode::CheckWordDialog:
            uiState.dialogTitle   = "Check Word";
            uiState.dialogPrompt  = "Word to check:";
            uiState.dialogPrompt2 = "";
            break;
        default:
            uiState.dialogTitle.clear();
            uiState.dialogPrompt.clear();
            uiState.dialogPrompt2.clear();
            break;
    }

    // Selection state for rendering
    uiState.selActive    = m_selection.active;
    uiState.selAnchorRow = m_selection.anchorRow;
    uiState.selAnchorCol = m_selection.anchorCol;

    // Menu state for rendering
    uiState.menuBarActive = (m_promptMode == PromptMode::MenuBar ||
                             m_promptMode == PromptMode::MenuOpen);
    uiState.menuOpen      = (m_promptMode == PromptMode::MenuOpen);
    uiState.activeMenu    = m_activeMenu;
    uiState.activeItem    = m_activeItem;

    // Overlay dialogs
    uiState.showHelp  = (m_promptMode == PromptMode::HelpScreen);
    uiState.showAbout = (m_promptMode == PromptMode::AboutScreen);

    // Font picker dialog (flat preset list)
    uiState.showFontDialog        = (m_promptMode == PromptMode::FontDialog);
    uiState.fontDialogPresetIdx   = m_fontDialogPresetIdx;
    uiState.fontDialogScrollTop   = m_fontDialogScrollTop;
    uiState.fontDialogActivePreset =
        static_cast<int>(m_fontSettings.face) * FontSizeCount()
        + IndexOfFontSize(m_fontSettings.size);

    uiState.themeDialogActive    = (m_promptMode == PromptMode::ThemeDialog);
    uiState.themeDialogFocusIdx  = m_themeDialogFocusIdx;
    uiState.themeDialogActiveIdx = static_cast<int>(m_themeName);

    {
        bool isHl = (m_promptMode == PromptMode::HighlightDialog);
        uiState.colorDialogActive      = (m_promptMode == PromptMode::ColorDialog || isHl);
        uiState.colorDialogIsHighlight = isHl;
        uiState.colorDialogFocusIdx    = m_colorDialogFocusIdx;
        uint8_t cur = isHl ? m_currentHighlight : m_currentColor;
        uiState.colorDialogCurrent     = (cur == CharFormat::Inherit)
                                         ? -1 : static_cast<int>(cur);
    }

    // Editor options
    uiState.wordWrap = m_wordWrap;

    // Find dialog state
    uiState.findDialogActive          = (m_promptMode == PromptMode::Find);
    uiState.findDialogCaseInsensitive = m_findCaseInsensitive;
    uiState.findDialogFocus           = m_findDialogFocus;

    // Word count: only compute when the status bar or modal needs it.
    uiState.showWordCount         = m_showWordCount;
    uiState.wordCountDialogActive = (m_promptMode == PromptMode::WordCountDialog);
    uiState.wordCount             = (m_showWordCount || uiState.wordCountDialogActive)
                                    ? CountWords(m_document->Buffer().Text())
                                    : 0;

    // Spell check
    uiState.spellCheckEnabled   = m_spellCheckEnabled;
    uiState.highlightMisspelled = m_highlightMisspelled;

    // Print dialog snapshot
    uiState.printDialogActive   = (m_promptMode == PromptMode::PrintDialog);
    uiState.printerList         = m_printerList;
    uiState.printPrinterIdx     = m_printPrinterIdx;
    uiState.printCopiesText     = m_printCopiesText;
    uiState.printFromText       = m_printFromText;
    uiState.printToText         = m_printToText;
    for (int i = 0; i < 4; ++i) uiState.printMarginText[i] = m_printMarginText[i];
    uiState.printAllPages       = m_printRequest.allPages;
    uiState.printOrientation    = (m_printRequest.orientation == PrintOrientation::Landscape) ? 1 : 0;
    uiState.printFocusField     = static_cast<int>(m_printFocus);

    // Margins dialog snapshot
    uiState.marginsDialogActive = (m_promptMode == PromptMode::MarginsDialog);
    for (int i = 0; i < 4; ++i) uiState.marginEditText[i] = m_marginEditText[i];
    uiState.marginFocusIdx      = m_marginFocusIdx;
    if (m_highlightMisspelled)
    {
        // Tokenize each visible buffer line and record misspelled spans.
        const auto& buf = m_document->Buffer();
        int firstRow = std::max(0, m_viewportTop);
        int lastRow  = std::min(buf.LineCount() - 1,
                                m_viewportTop + m_layout.EDITOR_ROWS - 1);
        for (int r = firstRow; r <= lastRow; ++r)
        {
            const std::string& line = buf.Line(r);
            int n = static_cast<int>(line.size());
            int i = 0;
            while (i < n)
            {
                unsigned char uc = static_cast<unsigned char>(line[i]);
                bool isWordChar = std::isalpha(uc) != 0 || line[i] == '\'';
                if (!isWordChar) { ++i; continue; }

                int start = i;
                while (i < n)
                {
                    unsigned char u = static_cast<unsigned char>(line[i]);
                    if (std::isalpha(u) == 0 && line[i] != '\'') break;
                    ++i;
                }
                int wstart = start;
                int wend   = i;
                while (wstart < wend && line[wstart]    == '\'') ++wstart;
                while (wend   > wstart && line[wend-1]  == '\'') --wend;
                if (wend <= wstart) continue;

                std::string word = line.substr(static_cast<size_t>(wstart),
                                               static_cast<size_t>(wend - wstart));
                if (!m_dictionary.Contains(word))
                    uiState.misspelledSpans.push_back({ r, wstart, wend - wstart });
            }
        }
    }

    m_ui->Draw(*m_screenBuffer, m_cursor, uiState);

    if (m_promptMode == PromptMode::None)
    {
        m_renderer->PaintBuffer(*m_screenBuffer);
        WysiwygRenderer::DrawContext ctx = BuildWysiwygDrawContext();

        // Auto-scroll the page so the cursor stays visible.
        m_wysiwygScrollPx  = m_wysiwyg->ClampScrollForCursor(ctx);
        ctx.viewportTopPx  = m_wysiwygScrollPx;

        m_wysiwyg->Draw(ctx);
        m_renderer->Present();
    }
    else
    {
        m_renderer->Render(*m_screenBuffer);
    }
}

WysiwygRenderer::DrawContext Application::BuildWysiwygDrawContext() const
{
    // RTF point sizes are absolute typographic units (1 pt = 1/72 in).
    // Render the WYSIWYG view at Windows' default 96 DPI so a "16 pt"
    // glyph is ~21 px tall on screen — matching LibreOffice / Word and
    // the printed page. Earlier phases rendered at 72 DPI to keep the
    // proportional view at the same pixel scale as the cell-grid
    // editor, but RetroDocWriter is no longer paired with cell-mode
    // rendering of the document and the 72 DPI shortcut made body
    // text noticeably smaller than every other word processor.
    const int dpi = 96;
    const int cw  = m_renderer ? m_renderer->CellWidth()  : 1;
    const int ch  = m_renderer ? m_renderer->CellHeight() : 1;

    WysiwygRenderer::DrawContext ctx{};
    ctx.buffer         = m_document ? &m_document->Buffer().Text() : nullptr;
    ctx.formatted      = m_document ? &m_document->Buffer() : nullptr;
    ctx.cursorRow      = m_cursor.row;
    ctx.cursorCol      = m_cursor.column;
    ctx.cursorVisible  = m_cursor.visible;
    ctx.selActive      = m_selection.active;
    ctx.selAnchorRow   = m_selection.anchorRow;
    ctx.selAnchorCol   = m_selection.anchorCol;
    ctx.margins        = m_margins;
    ctx.editorAreaPxX  = 0;
    ctx.editorAreaPxY  = m_layout.ROW_EDITOR_FIRST * ch;
    ctx.editorAreaPxW  = m_screenColumns * cw;
    ctx.editorAreaPxH  = (m_layout.ROW_EDITOR_LAST - m_layout.ROW_EDITOR_FIRST + 1) * ch;
    ctx.screenDpi      = dpi;
    ctx.viewportTopPx  = m_wysiwygScrollPx;
    ctx.face           = m_fontSettings.face;
    ctx.pointSize      = FontSizePoints(m_fontSettings.size);
    return ctx;
}

RtfWriter::Page Application::CurrentRtfPage() const
{
    // US Letter is currently the only supported paper size — matches the
    // hardcoded kPaperWidthIn / kPaperHeightIn in WysiwygRenderer.cpp.
    // The margins come from the per-document m_margins (Page > Margins...).
    RtfWriter::Page p;
    p.widthIn        = 8.5;
    p.heightIn       = 11.0;
    p.marginLeftIn   = m_margins.leftIn;
    p.marginRightIn  = m_margins.rightIn;
    p.marginTopIn    = m_margins.topIn;
    p.marginBottomIn = m_margins.bottomIn;
    return p;
}

void Application::UpdateCursorBlink()
{
    Uint64 now = SDL_GetTicks();
    if (now - m_lastBlinkTime >= BLINK_INTERVAL_MS)
    {
        m_cursor.visible = !m_cursor.visible;
        m_lastBlinkTime  = now;
        m_needsRedraw    = true;  // cursor visibility changed — repaint
    }
}
