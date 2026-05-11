#pragma once
#include "Cursor.h"
#include "platform/Window.h"
#include "render/FontSettings.h"
#include "render/ScreenBuffer.h"
#include "render/RetroRenderer.h"
#include "render/Theme.h"
#include "ui/Layout.h"
#include "ui/MenuDefs.h"
#include "ui/RetroUi.h"
#include "editor/FileDocument.h"
#include "editor/Selection.h"
#include "editor/UndoHistory.h"
#include "editor/WordWrap.h"
#include <SDL3/SDL.h>
#include <memory>
#include <string>

enum class PromptMode
{
    None,
    Open,
    SaveAs,
    Find,
    ConfirmExit,      // dirty: Y = save & exit, N = discard & exit, Esc = cancel
    ConfirmExitClean, // clean: Y = exit, N = cancel
    ConfirmNew,       // Y = new file without saving, N = cancel
    MenuBar,        // menu bar focused, no dropdown open
    MenuOpen,       // a dropdown menu is open
    HelpScreen,     // F1 help overlay
    AboutScreen,    // About dialog
    FontDialog,     // Font picker
    ConfirmWordWrap,// Y = wrap on, N = wrap off, Esc = cancel
};

class Application
{
public:
    Application();
    ~Application();

    int  Run();
    void OpenFile(const std::string& path);

private:
    // Main loop
    void ProcessEvents();
    void Update();
    void Render();

    // Input
    void HandleKeyDown(const SDL_KeyboardEvent& key);
    void HandlePromptKeyDown(const SDL_KeyboardEvent& key);
    void HandleMenuKeyDown(const SDL_KeyboardEvent& key);
    void HandleTextInput(const char* text);

    // Navigation helpers
    void UpdateSelection(bool shift);
    void MoveCursorUp();
    void MoveCursorDown();
    void MoveCursorLeft();
    void MoveCursorRight();

    // Editing
    void EraseSelection();
    void PushUndoBeforeEdit();
    void EnsureUndoBeforeInsert();
    void ApplyUndoState(const UndoState& s);
    void DoUndo();
    void DoRedo();

    // Clipboard
    void CopySelection();
    void CutSelection();
    void PasteClipboard();

    // File operations
    bool SaveDocument();
    void RequestExit();
    void NewFile();
    void StartOpenPrompt();
    void StartSaveAsPrompt();
    void StartFindPrompt();
    void CommitPrompt();
    void CancelPrompt();

    // Find
    void DoFind(const std::string& query);
    void FindNext();

    // Menu system
    void OpenMenuBar();
    void OpenMenu(int menuIdx);
    void CloseMenu();
    void ExecuteMenuItem(int menuIdx, int itemIdx);
    int  FirstSelectableItem(int menuIdx) const;
    int  NextSelectableItem(int menuIdx, int fromItem, int dir) const;

    // Cursor / viewport
    void UpdateCursorBlink();
    void ClampCursorToLine();
    void ScrollViewport();
    void UpdateWindowTitle();
    int  ComputeScreenRows(int cellHeight) const;
    int  ComputeScreenColumns(int cellWidth) const;
    void HandleWindowResized(int newW, int newH);

    // Font / window resizing
    void OpenFontDialog();
    void ApplyFontSettings(const FontSettings& settings);

    // Word wrap
    void OpenWordWrapDialog();
    void SetWordWrap(bool on);
    int  DisplayRowsForLine(int bufRow) const;

    // -----------------------------------------------------------------------
    bool       m_running             = false;
    bool       m_sdlInitialized      = false;
    Uint64     m_lastBlinkTime       = 0;
    int        m_viewportTop         = 0;
    int        m_viewportLeft        = 0;
    PromptMode m_promptMode          = PromptMode::None;
    std::string m_promptText;
    std::string m_statusMessage      = "Ready";
    std::string m_findQuery;
    int        m_findFromRow         = 0;
    int        m_findFromCol         = 0;
    bool       m_lastActionWasInsert = false;
    bool       m_exitAfterSave       = false; // chain SaveAs dialog into program exit
    bool       m_swallowNextTextInput = false;// drop the TEXT_INPUT that follows a confirm Y/N

    // Menu navigation state
    int        m_activeMenu          = -1;
    int        m_activeItem          = -1;

    // Font picker
    FontSettings m_fontSettings;
    int          m_fontDialogFaceIdx     = 0;
    int          m_fontDialogSizeIdx     = 0;
    int          m_fontDialogFocusColumn = 0;   // 0 = face, 1 = size

    // Cached window pixel dimensions and screen-buffer dimensions —
    // recomputed when the window is resized or the font changes.
    int          m_windowWidth           = 0;
    int          m_windowHeight          = 0;
    int          m_screenColumns         = 0;

    // Word wrap (display-only soft wrap)
    bool         m_wordWrap           = false;

    Layout                          m_layout;
    Theme                           m_theme;
    std::unique_ptr<Window>         m_window;
    std::unique_ptr<ScreenBuffer>   m_screenBuffer;
    std::unique_ptr<RetroRenderer>  m_renderer;
    std::unique_ptr<RetroUi>        m_ui;
    std::unique_ptr<FileDocument>   m_document;
    Cursor                          m_cursor;
    Selection                       m_selection;
    UndoHistory                     m_undoHistory;
};
