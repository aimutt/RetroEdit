#include "Application.h"
#include <algorithm>

// Window starts at this size. Both dimensions track live drag-resizes after
// startup; the screen-buffer column/row count is derived from current window
// pixels divided by the active font's cell pixel size.
static constexpr int    DEFAULT_WINDOW_WIDTH  = 1280;
static constexpr int    DEFAULT_WINDOW_HEIGHT = 800;
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

    m_window = std::make_unique<Window>("RetroEdit", m_windowWidth, m_windowHeight);
    if (!m_window->IsValid())
        return;
    SDL_SetWindowMinimumSize(m_window->GetWindow(), MIN_WINDOW_WIDTH, MIN_WINDOW_HEIGHT);

    // Pick up the actual window size in case the OS clamped or scaled it.
    SDL_GetWindowSize(m_window->GetWindow(), &m_windowWidth, &m_windowHeight);

    m_renderer = std::make_unique<RetroRenderer>(m_window->GetRenderer(), m_fontSettings);

    m_screenColumns = ComputeScreenColumns(m_renderer->CellWidth());
    int rows        = ComputeScreenRows(m_renderer->CellHeight());
    m_layout        = Layout(rows);

    m_screenBuffer = std::make_unique<ScreenBuffer>(m_screenColumns, rows);
    m_ui           = std::make_unique<RetroUi>(m_theme, m_layout);
    m_document     = std::make_unique<FileDocument>();

    SDL_StartTextInput(m_window->GetWindow());

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
        ProcessEvents();
        Update();
        Render();
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

void Application::ProcessEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
            case SDL_EVENT_QUIT:
                RequestExit();
                break;
            case SDL_EVENT_KEY_DOWN:
                HandleKeyDown(event.key);
                break;
            case SDL_EVENT_TEXT_INPUT:
                HandleTextInput(event.text.text);
                break;
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                HandleWindowResized(event.window.data1, event.window.data2);
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

    // Font picker — two-column navigation (Face | Size).
    if (m_promptMode == PromptMode::FontDialog)
    {
        const int faceCount = FontFaceCount();
        const int sizeCount = FontSizeCount();
        switch (key.scancode)
        {
            case SDL_SCANCODE_UP:
                if (m_fontDialogFocusColumn == 0)
                {
                    if (m_fontDialogFaceIdx > 0) --m_fontDialogFaceIdx;
                }
                else
                {
                    if (m_fontDialogSizeIdx > 0) --m_fontDialogSizeIdx;
                }
                break;
            case SDL_SCANCODE_DOWN:
                if (m_fontDialogFocusColumn == 0)
                {
                    if (m_fontDialogFaceIdx < faceCount - 1) ++m_fontDialogFaceIdx;
                }
                else
                {
                    if (m_fontDialogSizeIdx < sizeCount - 1) ++m_fontDialogSizeIdx;
                }
                break;
            case SDL_SCANCODE_TAB:
            case SDL_SCANCODE_LEFT:
            case SDL_SCANCODE_RIGHT:
                m_fontDialogFocusColumn = 1 - m_fontDialogFocusColumn;
                break;
            case SDL_SCANCODE_RETURN:
            {
                FontSettings choice{
                    static_cast<FontFace>(m_fontDialogFaceIdx),
                    FontSizeAt(m_fontDialogSizeIdx)
                };
                m_promptMode = PromptMode::None;
                if (!(choice == m_fontSettings))
                {
                    ApplyFontSettings(choice);
                    m_statusMessage = "Font changed";
                }
                else
                {
                    m_statusMessage = "Ready";
                }
                break;
            }
            case SDL_SCANCODE_ESCAPE:
                m_promptMode    = PromptMode::None;
                m_statusMessage = "Ready";
                break;
            default:
                break;
        }
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
            case SDL_SCANCODE_S: OpenMenu(2); return;   // Search
            case SDL_SCANCODE_V: OpenMenu(3); return;   // View
            case SDL_SCANCODE_R: OpenMenu(4); return;   // Run
            case SDL_SCANCODE_T: OpenMenu(5); return;   // Tools
            case SDL_SCANCODE_O: OpenMenu(6); return;   // Options
            case SDL_SCANCODE_H: OpenMenu(7); return;   // Help
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
                                            "    ", endRow, endCol);
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
    // Dirty-exit: 3-way Save / Discard / Cancel
    if (m_promptMode == PromptMode::ConfirmExit)
    {
        switch (key.scancode)
        {
            case SDL_SCANCODE_Y:
                // Save and exit. If no filename, chain into Save As dialog
                // and exit on its successful commit.
                m_promptMode = PromptMode::None;
                m_swallowNextTextInput = true;
                if (m_document->Filename().empty())
                {
                    m_exitAfterSave = true;
                    StartSaveAsPrompt();
                }
                else if (m_document->Save())
                {
                    UpdateWindowTitle();
                    m_running = false;
                }
                else
                {
                    m_statusMessage = "Error: could not save file";
                }
                break;
            case SDL_SCANCODE_N:
                m_promptMode = PromptMode::None;
                m_swallowNextTextInput = true;
                m_running    = false;
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

    // Word Wrap: 3-way Yes / No / Cancel
    if (m_promptMode == PromptMode::ConfirmWordWrap)
    {
        switch (key.scancode)
        {
            case SDL_SCANCODE_Y:
                m_promptMode = PromptMode::None;
                m_swallowNextTextInput = true;
                SetWordWrap(true);
                break;
            case SDL_SCANCODE_N:
                m_promptMode = PromptMode::None;
                m_swallowNextTextInput = true;
                SetWordWrap(false);
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

    // Clean-exit confirm and ConfirmNew: simple Y/N
    if (m_promptMode == PromptMode::ConfirmExitClean || m_promptMode == PromptMode::ConfirmNew)
    {
        switch (key.scancode)
        {
            case SDL_SCANCODE_Y:
                if (m_promptMode == PromptMode::ConfirmExitClean)
                    m_running = false;
                else
                    NewFile();
                m_promptMode = PromptMode::None;
                m_swallowNextTextInput = true;
                break;
            case SDL_SCANCODE_N:
                m_promptMode    = PromptMode::None;
                m_statusMessage = "Ready";
                m_swallowNextTextInput = true;
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

    // Text-input prompts (Open, SaveAs, Find)
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
        if (m_promptMode == PromptMode::Open  ||
            m_promptMode == PromptMode::SaveAs ||
            m_promptMode == PromptMode::Find)
        {
            m_promptText += text;
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
            m_document->Buffer().InsertChar(m_cursor.column, m_cursor.row, *p);
            ++m_cursor.column;
            m_document->MarkDirty();
            UpdateWindowTitle();
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

void Application::MoveCursorUp()
{
    if (!m_wordWrap)
    {
        if (m_cursor.row > 0)
        {
            --m_cursor.row;
            ClampCursorToLine();
            ScrollViewport();
        }
        return;
    }

    // Wrap mode — move one display row up.
    const std::string& curLine = m_document->Buffer().Line(m_cursor.row);
    auto starts = ComputeWrapStarts(curLine, m_screenColumns);
    int  segIdx = WrapSegmentForColumn(starts, m_cursor.column);
    int  offset = m_cursor.column - starts[segIdx];

    if (segIdx > 0)
    {
        // Same buffer line, previous segment.
        int destStart = starts[segIdx - 1];
        int destLen   = starts[segIdx] - destStart;       // includes consumed space
        m_cursor.column = destStart + std::min(offset, destLen);
    }
    else if (m_cursor.row > 0)
    {
        // Previous buffer line, last segment.
        --m_cursor.row;
        const std::string& prevLine = m_document->Buffer().Line(m_cursor.row);
        auto prevStarts = ComputeWrapStarts(prevLine, m_screenColumns);
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
    if (!m_wordWrap)
    {
        if (m_cursor.row < m_document->Buffer().LineCount() - 1)
        {
            ++m_cursor.row;
            ClampCursorToLine();
            ScrollViewport();
        }
        return;
    }

    // Wrap mode — move one display row down.
    const std::string& curLine = m_document->Buffer().Line(m_cursor.row);
    auto starts = ComputeWrapStarts(curLine, m_screenColumns);
    int  segIdx = WrapSegmentForColumn(starts, m_cursor.column);
    int  offset = m_cursor.column - starts[segIdx];

    if (segIdx + 1 < static_cast<int>(starts.size()))
    {
        // Same buffer line, next segment.
        int destStart = starts[segIdx + 1];
        int destEnd   = (segIdx + 2 < static_cast<int>(starts.size()))
                        ? starts[segIdx + 2]
                        : static_cast<int>(curLine.size());
        int destLen   = destEnd - destStart;
        m_cursor.column = destStart + std::min(offset, destLen);
    }
    else if (m_cursor.row < m_document->Buffer().LineCount() - 1)
    {
        // Next buffer line, first segment.
        ++m_cursor.row;
        const std::string& nextLine = m_document->Buffer().Line(m_cursor.row);
        auto nextStarts = ComputeWrapStarts(nextLine, m_screenColumns);
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

void Application::ApplyUndoState(const UndoState& s)
{
    m_document->Buffer().SetLines(s.lines);
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

    std::string text = m_document->Buffer().GetText(startRow, startCol, endRow, endCol);
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
    m_document->Buffer().InsertText(m_cursor.column, m_cursor.row, text, endRow, endCol);
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
    m_promptMode = PromptMode::Find;
    m_promptText = m_findQuery;
    m_statusMessage.clear();
}

void Application::DoFind(const std::string& query)
{
    if (query.empty()) return;
    m_findQuery = query;

    int foundRow = 0, foundCol = 0;
    if (m_document->Buffer().FindNext(query, m_findFromRow, m_findFromCol,
                                      foundRow, foundCol))
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
    m_document = std::make_unique<FileDocument>();
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
            if (m_document->SaveAs(m_promptText))
            {
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
    if (m_document->Save())
    {
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
                case 5: // Exit
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

        case 2: // Search
            switch (itemIdx)
            {
                case 0: StartFindPrompt(); break;
                case 1: FindNext();        break;
                default: break;
            }
            break;

        case 6: // Options
            switch (itemIdx)
            {
                case 0: OpenFontDialog();  break;   // Font...
                case 1: OpenWordWrapDialog(); break; // Word Wrap
                default: break;
            }
            break;

        case 7: // Help
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
    m_fontDialogFaceIdx     = static_cast<int>(m_fontSettings.face);
    m_fontDialogSizeIdx     = IndexOfFontSize(m_fontSettings.size);
    m_fontDialogFocusColumn = 0;
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
    m_renderer->SetFontSettings(settings);

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
    std::string title = "RetroEdit - " + m_document->DisplayName();
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
    uiState.textBuffer    = &m_document->Buffer();
    uiState.viewportTop   = m_viewportTop;
    uiState.viewportLeft  = m_viewportLeft;
    uiState.filename      = m_document->DisplayName();
    uiState.dirty         = m_document->IsDirty();
    uiState.statusMessage = m_statusMessage;

    // Modal dialog overlay for prompts and confirmations (replaces the old
    // bottom-of-screen prompt). Active only for text/confirm modes.
    uiState.dialogActive = (m_promptMode == PromptMode::Open             ||
                            m_promptMode == PromptMode::SaveAs           ||
                            m_promptMode == PromptMode::Find             ||
                            m_promptMode == PromptMode::ConfirmExit      ||
                            m_promptMode == PromptMode::ConfirmExitClean ||
                            m_promptMode == PromptMode::ConfirmNew       ||
                            m_promptMode == PromptMode::ConfirmWordWrap);
    uiState.dialogIsConfirm = (m_promptMode == PromptMode::ConfirmExit      ||
                               m_promptMode == PromptMode::ConfirmExitClean ||
                               m_promptMode == PromptMode::ConfirmNew       ||
                               m_promptMode == PromptMode::ConfirmWordWrap);
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
            uiState.dialogPrompt  = "Are you sure you want to exit RetroEdit?";
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

    // Font picker dialog (two-column: face | size)
    uiState.showFontDialog        = (m_promptMode == PromptMode::FontDialog);
    uiState.fontDialogFaceIdx     = m_fontDialogFaceIdx;
    uiState.fontDialogSizeIdx     = m_fontDialogSizeIdx;
    uiState.fontDialogFocusColumn = m_fontDialogFocusColumn;
    uiState.fontDialogActiveFace  = static_cast<int>(m_fontSettings.face);
    uiState.fontDialogActiveSize  = IndexOfFontSize(m_fontSettings.size);

    // Editor options
    uiState.wordWrap = m_wordWrap;

    m_ui->Draw(*m_screenBuffer, m_cursor, uiState);

    // Draw block cursor (hidden in all non-editor modes)
    if (m_cursor.visible && m_promptMode == PromptMode::None)
    {
        int screenRow;
        int screenCol;
        if (m_wordWrap)
        {
            int displayRow = 0;
            for (int r = m_viewportTop; r < m_cursor.row; ++r)
                displayRow += DisplayRowsForLine(r);
            auto starts = ComputeWrapStarts(
                m_document->Buffer().Line(m_cursor.row), m_screenColumns);
            int segIdx = WrapSegmentForColumn(starts, m_cursor.column);
            displayRow += segIdx;
            screenRow = m_layout.ROW_EDITOR_FIRST + displayRow;
            screenCol = m_cursor.column - starts[segIdx];
        }
        else
        {
            screenRow = m_cursor.row    - m_viewportTop  + m_layout.ROW_EDITOR_FIRST;
            screenCol = m_cursor.column - m_viewportLeft;
        }

        if (screenRow >= m_layout.ROW_EDITOR_FIRST && screenRow <= m_layout.ROW_EDITOR_LAST
            && screenCol >= 0 && screenCol < m_screenColumns)
        {
            m_screenBuffer->At(screenCol, screenRow).reverseVideo = true;
        }
    }

    m_renderer->Render(*m_screenBuffer);
}

void Application::UpdateCursorBlink()
{
    Uint64 now = SDL_GetTicks();
    if (now - m_lastBlinkTime >= BLINK_INTERVAL_MS)
    {
        m_cursor.visible = !m_cursor.visible;
        m_lastBlinkTime  = now;
    }
}
