#pragma once
#include "app/Cursor.h"
#include "platform/Window.h"
#include "render/FontSettings.h"
#include "render/ScreenBuffer.h"
#include "render/RetroRenderer.h"
#include "render/Theme.h"
#include "render/WysiwygRenderer.h"
#include "ui/Layout.h"
#include "ui/MenuDefs.h"
#include "ui/RetroUi.h"
#include "editor/Dictionary.h"
#include "editor/RichFileDocument.h"
#include "editor/FileSettings.h"
#include "editor/Selection.h"
#include "editor/RichUndoHistory.h"
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
    ThemeDialog,    // Theme picker (Options > Theme...)
    ColorDialog,    // Per-character text-color picker (Format > Text Color...)
    HighlightDialog,// Per-character highlight picker (Format > Highlight Color...)
    ConfirmWordWrap,// Y = wrap on, N = wrap off, Esc = cancel
    WordCountDialog,// shows live word count + status-bar toggle checkbox
    AddWordDialog,  // text input -> Dictionary::AddWord
    RemoveWordDialog,// text input -> Dictionary::RemoveWord
    CheckWordDialog,// text input -> Dictionary::Contains, result in status bar
    PrintDialog,    // full print dialog (printer, copies, range, orientation, margins)
    MarginsDialog,  // WYSIWYG page margins (per-document)
    ConfirmSaveAsRtf, // Ctrl+S on a .txt that has formatting: Y = SaveAs.rtf, N = flatten + save .txt
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
    void HandleMouseUp  (int cellCol, int cellRow, Uint8 button);
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
    // WYSIWYG -> WYSIWYG chars-per-line, word-wrap -> screen columns,
    // otherwise 0 (treat as no wrap — Up/Down skip whole buffer rows).
    int  NavigationWrapWidth() const;

    // Editing
    void EraseSelection();
    void PushUndoBeforeEdit();
    void EnsureUndoBeforeInsert();
    void ApplyUndoState(const RichUndoState& s);
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

    // WYSIWYG scrollbar — used by arrow clicks and thumb drag to keep the
    // cursor on the topmost visible row after the user scrolls.
    void UpdateCursorToViewportTop();

    // Font / window resizing
    void OpenFontDialog();
    void ApplyFontSettings(const FontSettings& settings);

    // Theme picker
    void OpenThemeDialog();
    void ApplyThemeDialogSelection();

    // Per-character text color
    void OpenColorDialog();
    void ApplyColorDialogSelection();

    // Per-character background highlight (reuses the color picker dialog)
    void OpenHighlightDialog();
    void ApplyHighlightDialogSelection();

    // Insert a paragraph break that also forces the next paragraph onto a
    // new page (Ctrl+Enter / Format > Insert Page Break).
    void InsertPageBreak();

    // Builds a DrawContext snapshot of the current document state. Used by
    // Render() and by the cursor-arrow keys (MoveCursorUp/Down) so both
    // see the exact same visual layout.
    WysiwygRenderer::DrawContext BuildWysiwygDrawContext() const;

    // Builds the page-geometry struct for the RTF writer from the
    // document's current margins. US Letter is hardcoded as the paper
    // size (matches WysiwygRenderer's kPaperWidthIn/kPaperHeightIn).
    RtfWriter::Page  CurrentRtfPage() const;

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
    void ToggleShowMargins();
    void CheckJustCompletedWord();
    void SaveUserDictionary();

    // Print
    void OpenPrintDialog();
    void ClosePrintDialog(bool commit);
    void PrintCycleField(int dir);                  // Tab / Shift-Tab
    void PrintAdjustField(int dir);                 // Up / Down (or </> printer)
    void PrintTextEdit(char ch);                    // digit or '.'
    void PrintBackspace();

    // Per-character formatting (Format menu / Ctrl+B / Ctrl+I / Ctrl+U).
    // With a non-empty selection the bit is XOR-toggled across the range
    // (smart toggle — clear if all selected chars have it, else set). With
    // no selection the bit toggles in m_currentStyle for next-typed input.
    void ToggleBold();
    void ToggleItalic();
    void ToggleUnderline();
    void ToggleStrikethrough();
    void ApplyStyleAction(uint8_t bit);

    // Margins dialog
    void OpenMarginsDialog();
    void CloseMarginsDialog(bool commit);
    void MarginCycleField(int dir);
    void MarginAdjustField(int dir);
    void MarginTextEdit(char ch);
    void MarginBackspace();

    // Global app settings (persisted under %LOCALAPPDATA%\RetroEdit\)
    void LoadGlobalSettings();
    void SaveGlobalSettings();
    // Theme switching: updates m_themeName, rebuilds m_theme, redraws, and
    // persists the choice to config.ini. No-op when name == current.
    void ApplyTheme(ThemeName name);

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

    // Two separate font settings — the chrome (menus, status bar, dialogs,
    // function-key bar) and the document (the WYSIWYG page contents) draw
    // independently. The Font dialog (Options > Font...) targets the
    // document font ONLY; the chrome font is fixed at startup so menus and
    // dialog text don't resize when the user picks a different document font.
    //
    // Flat preset list encoding (face_idx * FontSizeCount() + size_idx) —
    // see EditorUiState comment in RetroUi.h.
    FontSettings m_chromeFontSettings;    // monospace cell-grid (RetroRenderer)
    FontSettings m_documentFontSettings;  // proportional WYSIWYG (WysiwygRenderer)
    int          m_fontDialogPresetIdx = 0;  // focused row
    int          m_fontDialogScrollTop = 0;  // first visible row

    // Theme picker (Options > Theme...)
    int          m_themeDialogFocusIdx   = 0;

    // Scrollbar thumb drag. Reusable across dialogs — only one scrollbar is
    // ever active at once, so the state is flat and tagged by the prompt mode
    // that owns it. A future dialog adopting drag just (a) calls
    // RetroUi::HitTestScrollbar, (b) populates these fields on mousedown with
    // its own geometry, (c) adds its PromptMode to the switch in
    // HandleMouseMotion that writes the new scrollTop back.
    bool       m_scrollbarDragActive       = false;
    PromptMode m_scrollbarDragOwner        = PromptMode::None;
    int        m_scrollbarDragX            = 0;
    int        m_scrollbarDragY            = 0;
    int        m_scrollbarDragHeight       = 0;
    int        m_scrollbarDragTotalItems   = 0;
    int        m_scrollbarDragVisibleItems = 0;
    int        m_scrollbarDragGrabOffset   = 0;

    // Per-character text color (Format > Text Color...). m_currentColor
    // applies to next-typed input when no selection is active.
    uint8_t      m_currentColor          = CharFormat::Inherit;
    int          m_colorDialogFocusIdx   = 0;

    // Per-character background highlight (Format > Highlight Color...).
    // Same UX as text color but writes a different CharFormat field.
    uint8_t      m_currentHighlight      = CharFormat::Inherit;

    // Per-character face/size for next-typed input when the Font dialog is
    // committed without a selection (mirrors m_currentColor's pattern).
    // Inherit means "follow the document default" (m_documentFontSettings) —
    // is the initial state. Picking a face/size in the dialog without a
    // selection sets these; existing text (which carries Inherit-face/size
    // by default) is unchanged. To restyle existing text the user must
    // Select All first, then pick a face/size — the dialog pins the
    // selected range via SetFaceInRange / SetSizeInRange.
    uint8_t      m_currentFace           = CharFormat::Inherit;
    uint8_t      m_currentSize           = CharFormat::Inherit;

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

    // WYSIWYG-only: show/hide the dim margin guides drawn inside each page
    // rectangle. Persisted globally as `show_margins` in config.ini; defaults
    // to true so existing users keep the current look on first launch with
    // the new toggle.
    bool         m_showMargins          = true;

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

    WysiwygMargins   m_margins;
    int              m_wysiwygScrollPx = 0;
    std::unique_ptr<WysiwygRenderer> m_wysiwyg;
    // Margins dialog edit-in-progress strings (top, bottom, left, right)
    std::string      m_marginEditText[4];
    int              m_marginFocusIdx  = 0;

    Layout                          m_layout;
    ThemeName                       m_themeName = ThemeName::Green;
    Theme                           m_theme;
    std::unique_ptr<Window>         m_window;
    std::unique_ptr<ScreenBuffer>   m_screenBuffer;
    std::unique_ptr<RetroRenderer>  m_renderer;
    std::unique_ptr<RetroUi>        m_ui;
    std::unique_ptr<RichFileDocument> m_document;
    Cursor                          m_cursor;
    Selection                       m_selection;
    RichUndoHistory                 m_undoHistory;

    // Per-character style applied to next-typed input when no selection is
    // active. Toggled by Ctrl+B / Ctrl+I / Ctrl+U and the Format menu items
    // (Step 5 wires the UI). Stored here so Render() can surface a status-bar
    // indicator showing the active style.
    uint8_t                         m_currentStyle = 0;
};
