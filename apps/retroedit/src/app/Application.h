#pragma once
#include "app/Cursor.h"
#include "platform/Window.h"
#include "render/FontSettings.h"
#include "render/ScreenBuffer.h"
#include "render/RetroRenderer.h"
#include "render/Theme.h"
#include "ui/Layout.h"
#include "ui/MenuDefs.h"
#include "ui/RetroUi.h"
#include "editor/Dictionary.h"
#include "editor/FileDocument.h"
#include "editor/FileSettings.h"
#include "editor/Selection.h"
#include "editor/UndoHistory.h"
#include "editor/WordWrap.h"
#include "platform/Print.h"
#include <SDL3/SDL.h>
#include <memory>
#include <string>
#include <vector>

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
    WordCountDialog,// shows live word count + status-bar toggle checkbox
    AddWordDialog,  // text input -> Dictionary::AddWord
    RemoveWordDialog,// text input -> Dictionary::RemoveWord
    CheckWordDialog,// text input -> Dictionary::Contains, result in status bar
    PrintDialog,    // full print dialog (printer, copies, range, orientation, margins)
};

// Fields in the Print dialog, ordered for Tab cycling.
enum class PrintField {
    Printer,
    Copies,
    RangeMode,    // All vs Custom (Space toggles)
    RangeFrom,
    RangeTo,
    Orientation,  // Portrait vs Landscape (Space toggles)
    MarginTop,
    MarginBottom,
    MarginLeft,
    MarginRight,
    Count         // sentinel — count of cyclable fields
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
    void DispatchEvent(const SDL_Event& event);
    void Update();
    void Render();

    // Input
    void HandleKeyDown(const SDL_KeyboardEvent& key);
    void HandlePromptKeyDown(const SDL_KeyboardEvent& key);
    void HandleMenuKeyDown(const SDL_KeyboardEvent& key);
    void HandleTextInput(const char* text);
    void HandleMouseDown(int cellCol, int cellRow, Uint8 button);
    void HandleMouseMotion(int cellCol, int cellRow);
    // Returns true if the click was consumed by an active dialog.
    bool HandleDialogMouseDown(int cellCol, int cellRow);

    // Shared by keyboard Y/N handlers and mouse-click Yes/No on confirm dialogs.
    // Reads m_promptMode to know which confirm is active, then resets it.
    void ResolveConfirmYes();
    void ResolveConfirmNo();

    // Shared by keyboard Enter and mouse Apply on the Font dialog.
    void ApplyFontDialogSelection();

    // Navigation helpers
    void UpdateSelection(bool shift);
    void MoveCursorUp();
    void MoveCursorDown();
    void MoveCursorLeft();
    void MoveCursorRight();
    // Effective wrap width (characters) for visual cursor navigation:
    // word-wrap on -> screen columns, otherwise 0 (treat as no wrap — Up/Down
    // skip whole buffer rows).
    int  NavigationWrapWidth() const;

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

    // Word count
    void OpenWordCountDialog();
    void ToggleStatusBarWordCount();

    // Spell check
    void OpenAddWordDialog();
    void OpenRemoveWordDialog();
    void OpenCheckWordDialog();
    void ToggleSpellCheck();
    void ToggleHighlightMisspelled();
    void CheckJustCompletedWord();
    void SaveUserDictionary();

    // Print
    void OpenPrintDialog();
    void ClosePrintDialog(bool commit);
    void PrintCycleField(int dir);                  // Tab / Shift-Tab
    void PrintAdjustField(int dir);                 // Up / Down (or </> printer)
    void PrintTextEdit(char ch);                    // digit or '.'
    void PrintBackspace();

    // Global app settings (persisted under %LOCALAPPDATA%\RetroEdit\)
    void LoadGlobalSettings();
    void SaveGlobalSettings();

    // Per-file settings sidecar (FileSettings). Add new persisted settings
    // by extending the two Capture/Apply helpers — the I/O paths above and
    // below already call them for every save/open.
    void CaptureFileSettings(FileSettings& s) const;
    void ApplyFileSettings(const FileSettings& s);
    void WriteSidecarForCurrentDocument();
    void LoadSidecarForCurrentDocument();

    // -----------------------------------------------------------------------
    bool       m_running             = false;
    bool       m_sdlInitialized      = false;
    bool       m_needsRedraw         = true;  // paint first frame; set by event/blink handlers
    Uint64     m_lastBlinkTime       = 0;
    int        m_viewportTop         = 0;
    int        m_viewportLeft        = 0;
    PromptMode m_promptMode          = PromptMode::None;
    std::string m_promptText;
    std::string m_statusMessage      = "Ready";
    std::string m_findQuery;
    int        m_findFromRow         = 0;
    int        m_findFromCol         = 0;
    bool       m_findCaseInsensitive = false; // persisted for the session
    int        m_findDialogFocus     = 0;     // 0 = input field, 1 = checkbox
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

    // Word count status-bar display toggle (persisted per file)
    bool         m_showWordCount      = false;

    // Spell check (global; persisted under %LOCALAPPDATA%\RetroEdit\)
    bool         m_spellCheckEnabled    = false;
    bool         m_highlightMisspelled  = false;
    Dictionary   m_dictionary;

    // Print dialog state — populated when OpenPrintDialog runs, persists
    // across invocations within the session so the user's last choices stick.
    PrintRequest             m_printRequest;
    std::vector<std::string> m_printerList;
    int                      m_printPrinterIdx = 0;
    PrintField               m_printFocus      = PrintField::Printer;
    std::string              m_printCopiesText;
    std::string              m_printFromText;
    std::string              m_printToText;
    std::string              m_printMarginText[4]; // top, bottom, left, right

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
