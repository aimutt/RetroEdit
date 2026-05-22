# RetroEdit Project Plan

> **RetroEdit is the plain-text product** in a two-product family. For the WYSIWYG sibling that shares this repo's core code but ships as a separate EXE, see [RetroDocWriter.md](RetroDocWriter.md).

## Table of Contents

- [Overview](#overview)
- [Project Goals](#project-goals)
- [Core Concept](#core-concept)
- [Recommended Technology Stack](#recommended-technology-stack)
- [Why SDL Instead of a Traditional GUI Framework](#why-sdl-instead-of-a-traditional-gui-framework)
- [Application Architecture](#application-architecture)
- [Virtual Screen Model](#virtual-screen-model)
- [Render Loop and Idle Cost](#render-loop-and-idle-cost)
- [Suggested Project Structure](#suggested-project-structure)
- [Core Design Patterns](#core-design-patterns)
- [Extensibility Strategy](#extensibility-strategy)
- [Development Roadmap](#development-roadmap)
- [Stage 1 Detailed Plan](Stage%201.md)
- [AI Integration Plan](#ai-integration-plan)
- [Build Process](#build-process)
- [Visual Style Guidelines](#visual-style-guidelines)
- [Keyboard-First User Experience](#keyboard-first-user-experience)
- [Additional Considerations](#additional-considerations)
- [First Milestone](#first-milestone)
- [Long-Term Vision](#long-term-vision)

---

## Overview

**RetroEdit** is a modern cross-platform desktop text editor designed to look and feel like a green monochrome editor from the early 1980s.

The first version will target **Windows x64**, but the architecture should support future builds for Linux and macOS.

The project should be written in **C++20** and designed in stages so that it can begin as a simple text editor and eventually grow into a lightweight retro-style IDE with local AI assistance.

---

## Project Goals

RetroEdit should:

- Feel like an early 1980s text editor, not a modern GUI application with green colors.
- Use keyboard-first interaction.
- Support mouse input only as a convenience.
- Render its own text-mode interface.
- Use a green monochrome CRT-inspired visual style.
- Be written in clean, modular C++20.
- Be easy to extend with future features.
- Eventually support code editing, compiling, and local AI helper features.

---

## Core Concept

RetroEdit should simulate a text-mode computer environment inside a modern desktop window.

Example layout:

```text
+------------------------------------------------------------------------------+
| File  Edit  Search  View  Run  Tools  Help                                    |
+------------------------------------------------------------------------------+
|                                                                              |
|  Main editing area                                                            |
|                                                                              |
|  Text is edited here using a fixed-width retro character grid.                |
|                                                                              |
|                                                                              |
+------------------------------------------------------------------------------+
| F1 Help | F2 Save | F3 Open | F5 Run | F10 Menu | ESC Exit                    |
+------------------------------------------------------------------------------+
```

The application should draw its own menus, dialogs, status bars, cursor, selection blocks, and command areas.

This keeps the experience authentic and prevents the program from feeling like a normal modern GUI application.

---

## Recommended Technology Stack

| Area | Recommended Choice | Notes |
|---|---|---|
| Language | C++20 | Modern C++ with strong structure and maintainability |
| Windowing | SDL3 | Cross-platform window, input, and rendering support |
| Rendering | SDL Renderer first, optional OpenGL later | Start simple; add shader effects later |
| Build System | CMake | Standard cross-platform C++ build system |
| Testing | Catch2 or GoogleTest | Unit tests for editor logic |
| Configuration | JSON or TOML | Themes, key bindings, and settings |
| Packaging | CPack or simple ZIP package first | Keep early distribution simple |
| Future AI | Local HTTP provider | Can connect to llama.cpp server, Ollama, or similar |

---

## Why SDL Instead of a Traditional GUI Framework

Traditional GUI frameworks such as Qt, wxWidgets, or native Windows controls are powerful, but they naturally produce modern-looking applications.

RetroEdit should feel like a complete retro text-mode environment.

SDL is a better starting point because it provides:

- A cross-platform window.
- Keyboard and mouse input.
- Low-level rendering control.
- Freedom to draw every interface element manually.
- A path to later support Linux and macOS.

The goal is not to build a form-based desktop app.  
The goal is to build a retro editor environment.

---

## Application Architecture

High-level flow:

```text
Application
  |
  +-- Input System
  |     |
  |     +-- Command Registry
  |             |
  |             +-- Editor Model
  |
  +-- UI Components
  |     |
  |     +-- Menu Bar
  |     +-- Status Bar
  |     +-- Dialogs
  |
  +-- Screen Buffer
        |
        +-- Retro Renderer
              |
              +-- SDL Window
```

The editor logic should not know about SDL directly.

SDL should mostly live in the platform and rendering layers. This keeps the editor core easier to test and easier to port later.

---

## Virtual Screen Model

RetroEdit should be built around a virtual character-cell screen.

Example:

```text
Screen size: 80 columns x 25 rows

Each screen cell contains:
- character
- foreground color
- background color
- brightness
- blink flag
- reverse-video flag
- optional style flags
```

Conceptual structure:

```cpp
struct ScreenCell
{
    char32_t character;
    Color foreground;
    Color background;
    bool bright;
    bool blink;
    bool reverseVideo;
};
```

The renderer draws the screen buffer to the window.  
The rest of the application writes characters and attributes into the screen buffer.

This approach makes the application feel more like a real text-mode system.

---

## Render Loop and Idle Cost

A retro text editor should sit quietly when nothing is happening. The naïve "poll events, update, render, repeat" loop common in SDL examples will pin a GPU at 50% and a CPU core at 9% at idle, because nothing throttles `SDL_RenderPresent` and nothing sleeps the thread.

RetroEdit's main loop is built around three layered controls so that idle cost stays at roughly 1% CPU / 1% GPU even with a full screen of text loaded.

### 1. VSync

`Window` calls `SDL_SetRenderVSync(renderer, 1)` immediately after `SDL_CreateWindowAndRenderer`. `SDL_RenderPresent` now blocks until the next display vblank, capping the maximum frame rate at the monitor's refresh rate. This alone takes the loop from thousands of frames per second to ~60.

### 2. Event-driven sleep with `SDL_WaitEventTimeout`

The main loop in `Application::Run` does not busy-poll. Each iteration it computes the time remaining until the next cursor-blink tick (the only periodic redraw the editor needs) and calls `SDL_WaitEventTimeout` with that deadline. The thread is genuinely descheduled by the OS — it consumes no cycles — until either an input event arrives or the timeout expires. When an event does arrive, a follow-up `SDL_PollEvent` drain consumes any queued companions (for example a `KEY_DOWN` and its `TEXT_INPUT` pair) so they coalesce into a single render.

### 3. `m_needsRedraw` dirty flag

VSync caps *how often* the editor can repaint. The dirty flag controls *whether* it repaints at all. Every event the application handles (key, text input, window resize, quit) sets `m_needsRedraw = true`. The cursor-blink toggle in `UpdateCursorBlink` sets it. Unhandled events such as `MOUSEMOTION` deliberately do not set it — passive mouse movement over the window costs only the wake-up from `SDL_WaitEventTimeout`, no render. `Render()` runs only when the flag is set, then clears it.

### Resulting behavior

| State | Frames per second | Notes |
|---|---|---|
| Idle, no input | ~2 | One per cursor-blink toggle (500 ms interval) |
| Active typing | up to display refresh | Each keystroke wakes the loop and renders within one vblank |
| Window resize | up to display refresh | Existing reflow path repaints on every resize event |
| Mouse motion only | 0 | Unhandled event — wakes the thread but no render |

### What this does *not* do

These were considered and intentionally deferred — current idle cost is low enough that the added complexity is not justified:

- **Dirty-rect rendering.** Repainting only changed cells would shave full-screen render time, but the cell loop is already fast enough at 60 fps active and 2 fps idle.
- **Per-cell color-state coalescing.** `RetroRenderer::Render` issues one `SDL_SetRenderDrawColor` per cell. With ~80×40 cells and the loop running only on demand, this is well within budget.
- **Glyph cache eviction.** The cache grows monotonically with unique characters seen. On normal editing sessions it stays small. Revisit only if pasting very large multilingual documents becomes a use case.

The guiding rule: **a GUI at rest should do nothing.** Three knobs cooperate to make that true — vsync caps the ceiling, event-wait lets the OS sleep the thread, and the dirty flag skips work that would have no visible effect.

---

## Suggested Project Structure

Plain module map:

```text
RetroEdit
  |
  +-- src
  |     |
  |     +-- app
  |     +-- platform
  |     +-- render
  |     +-- editor
  |     +-- ui
  |     +-- services
  |
  +-- assets
  |     |
  |     +-- fonts
  |     +-- themes
  |
  +-- tests
```

Suggested folder layout:

```text
RetroEdit/
  CMakeLists.txt
  README.md

  src/
    main.cpp

    app/
      Application.cpp
      Application.h
      CommandRegistry.cpp
      CommandRegistry.h
      EventBus.cpp
      EventBus.h

    platform/
      Window.cpp
      Window.h
      Input.cpp
      Input.h

    render/
      RetroRenderer.cpp
      RetroRenderer.h
      ScreenBuffer.cpp
      ScreenBuffer.h
      FontAtlas.cpp
      FontAtlas.h
      Theme.cpp
      Theme.h

    editor/
      TextBuffer.cpp
      TextBuffer.h
      Cursor.cpp
      Cursor.h
      Selection.cpp
      Selection.h
      UndoRedo.cpp
      UndoRedo.h
      FileDocument.cpp
      FileDocument.h

    ui/
      MenuBar.cpp
      MenuBar.h
      StatusBar.cpp
      StatusBar.h
      Dialog.cpp
      Dialog.h
      CommandPalette.cpp
      CommandPalette.h

    services/
      FileService.cpp
      FileService.h
      SettingsService.cpp
      SettingsService.h
      AiService.cpp
      AiService.h

  assets/
    fonts/
    themes/

  tests/
```

---

## Core Design Patterns

### Command Pattern

Every action should be represented as a command.

Examples:

```text
file.new
file.open
file.save
file.save_as
edit.copy
edit.cut
edit.paste
edit.undo
edit.redo
search.find
search.find_next
view.toggle_line_numbers
ai.explain_selection
build.compile
build.run
```

Benefits:

- Keyboard shortcuts can call commands.
- Menus can call commands.
- Mouse actions can call commands.
- Future scripting can call commands.
- Future plug-ins can register commands.

This should be one of the earliest systems created.

---

### Event Bus / Observer Pattern

The event system can notify parts of the app when something changes.

Examples:

```text
document.modified
document.saved
cursor.moved
selection.changed
theme.changed
command.executed
```

This keeps components loosely connected.

For example, the editor does not need to directly update the status bar.  
It can emit an event, and the status bar can respond.

---

### Strategy Pattern

Use strategy-style interfaces for systems that may change later.

Examples:

```text
IRenderer
IThemeProvider
IAiProvider
IBuildProvider
IFileDialogProvider
```

This allows future replacement or expansion without rewriting the core editor.

---

### Model/View Separation

The text buffer should not know how it is drawn.

Suggested separation:

```text
TextBuffer       -> stores and edits text
EditorView       -> decides what part of the buffer is visible
RetroRenderer    -> draws characters to the screen
InputSystem      -> translates key input into commands
CommandRegistry  -> executes editor actions
```

---

## Extensibility Strategy

RetroEdit should be designed so future features can be added without rewriting the application.

Recommended extensibility points:

| Extension Area | Purpose |
|---|---|
| Commands | Add new actions |
| Key bindings | Let users customize shortcuts |
| Themes | Support different phosphor styles |
| Renderers | Support SDL renderer first, OpenGL later |
| AI providers | Support llama.cpp, Ollama, or other engines |
| Build providers | Support different compilers |
| Language services | Future syntax and IDE features |
| Panels | Add output window, AI chat, search results, etc. |

---

## Development Roadmap

### Stage 1 — Retro Shell

Detailed implementation guide: [Stage 1 Detailed Plan](Stage%201.md)

Goal: create the visual foundation.

Features:

- SDL window.
- 80x25 or 80x30 virtual screen.
- Green monochrome color palette.
- Bitmap-style fixed-width font.
- Static menu bar.
- Static status bar.
- Blinking block cursor.
- Basic keyboard event handling.

Deliverable:

```text
A window opens and looks like an early 1980s green monochrome editor.
```

---

### Stage 2 — Basic Text Editing

Goal: support basic editing.

Features:

- Type text.
- Move cursor with arrow keys.
- Backspace and Delete.
- Enter for new lines.
- Open plain text file.
- Save plain text file.
- Show filename, line, and column in the status bar.

Deliverable:

```text
A simple usable text editor.
```

---

### Stage 3 — Editing Features

Goal: make the editor practical.

Features:

- Text selection.
- Cut, copy, and paste.
- Undo and redo.
- Find text.
- Find next.
- Save As.
- New file.
- Warning when closing a modified file.

Deliverable:

```text
A practical editor suitable for everyday plain text editing.
```

---

### Stage 4 — Retro Menus and Dialogs

Goal: add authentic text-mode interaction.

Features:

- Alt-key menu navigation.
- Function key command bar.
- Text-mode modal dialogs.
- File open dialog rendered inside the app.
- Save confirmation dialog.
- Help screen.
- About screen.

Example shortcuts:

```text
Alt+F  -> File menu
Alt+E  -> Edit menu
Alt+S  -> Search menu
F1     -> Help
F2     -> Save
F3     -> Open
F5     -> Run
F10    -> Menu
ESC    -> Cancel or exit menu
```

Deliverable:

```text
A keyboard-first retro editor experience.
```

---

### Stage 5 — Extensibility Foundation

Goal: prepare the app for long-term growth.

Features:

- Command registry.
- Configurable key bindings.
- Theme files.
- Settings file.
- Menu definitions loaded from configuration.
- Basic logging.
- Unit tests for editor logic.

Deliverable:

```text
A maintainable editor foundation that can grow into an IDE.
```

---

### Stage 6 — IDE Foundation

Goal: add early code-editing features.

Features:

- Syntax highlighting.
- Optional line numbers.
- Build command.
- Run command.
- Output panel.
- Error list.
- Jump to error location.
- Project folder support.

Deliverable:

```text
A lightweight retro-style code editor.
```

---

### Stage 7 — Local AI Helper

Goal: add optional local AI assistance.

Features:

- AI provider interface.
- Local HTTP provider.
- AI chat panel.
- Explain selected text.
- Summarize current file.
- Suggest improvements.
- Generate comments.
- Ask questions about the current document.

Deliverable:

```text
A retro editor with modern local AI assistance.
```

---

## AI Integration Plan

The AI system should be optional and modular.

The editor should not depend directly on a specific AI engine.

Suggested interface:

```cpp
class IAiProvider
{
public:
    virtual ~IAiProvider() = default;

    virtual std::string SendPrompt(const std::string& prompt) = 0;
};
```

Possible implementations:

```text
LocalHttpAiProvider
  -> llama.cpp server
  -> Ollama
  -> other local inference server
```

AI command examples:

```text
ai.ask
ai.explain_selection
ai.summarize_file
ai.suggest_fix
ai.generate_comment
ai.refactor_selection
```

AI should appear as a retro-style panel, not a modern chat widget.

Example layout:

```text
+------------------------------------------------------------------------------+
| File  Edit  Search  Run  Tools  AI  Help                                      |
+------------------------------------------------------------------------------+
|                                                                              |
|  Main editor pane                                                             |
|                                                                              |
+--------------------------------------+---------------------------------------+
| Status: Modified | Line 12 Col 4      | AI: Ready                             |
+--------------------------------------+---------------------------------------+
```

---

## Build Process

The build process should be simple, repeatable, and robust.

Recommended approach:

```text
CMake + vcpkg or FetchContent
```

Initial Windows x64 goals:

- Visual Studio 2022 support.
- Debug and Release builds.
- CMake configure/build from command line.
- Optional VS Code integration.
- All third-party dependencies documented.

Example commands:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

Later build goals:

- GitHub Actions CI.
- Automated tests.
- Release ZIP package.
- Installer package.
- Linux build.
- macOS build.

---

## Visual Style Guidelines

RetroEdit should feel like a real text-mode program.

### Required Style Elements

- Green phosphor text.
- Black or very dark green background.
- Fixed-width bitmap-style font.
- Block cursor.
- Reverse-video selection.
- Menu bar drawn with text.
- Function-key command bar.
- Text-mode dialogs.
- Box-drawing characters.
- Optional scanlines.
- Optional glow or afterimage effect.

### Avoid

- Native Windows menus.
- Native Windows file dialogs in the final design.
- Modern toolbar icons.
- Ribbon interface.
- Rounded buttons.
- Floating modern panels.
- Mouse-dependent workflows.
- Modern color-heavy syntax themes by default.

---

## Keyboard-First User Experience

The editor should be fully usable without a mouse.

Recommended early shortcut model:

| Key | Action |
|---|---|
| F1 | Help |
| F2 | Save |
| F3 | Open |
| F5 | Run or build/run later |
| F10 | Activate menu |
| Esc | Cancel menu/dialog or prompt before exit |
| Ctrl+N | New file |
| Ctrl+O | Open |
| Ctrl+S | Save |
| Ctrl+F | Find |
| Ctrl+Z | Undo |
| Ctrl+Y | Redo |
| Ctrl+C | Copy |
| Ctrl+X | Cut |
| Ctrl+V | Paste |

The function key bar should always show important commands.

Example:

```text
 F1 Help   F2 Save   F3 Open   F5 Run   F6 Search   F10 Menu   Esc Exit
```

---

## Additional Considerations

### Text Encoding

Use UTF-8 internally, but provide options for retro compatibility.

Possible future modes:

- UTF-8
- ASCII-only
- Code page 437 rendering

### Fonts

Use a bitmap or bitmap-inspired font.

Good options:

- IBM VGA style font.
- DOS-style 8x16 font.
- Custom bundled font atlas.

### Theming

Support theme files so the visual style can evolve.

Possible themes:

- Green phosphor.
- Amber monochrome.
- White monochrome.
- High-contrast DOS.
- IBM blue IDE style.

### File Safety

The editor should protect user work.

Important features:

- Unsaved changes warning.
- Temporary backup files.
- Autosave option.
- Recovery file after crash.

### Testing

Editor logic should be testable without SDL.

Good test targets:

- Text insertion.
- Text deletion.
- Cursor movement.
- Selection logic.
- Undo/redo.
- File loading and saving.
- Command execution.

### Logging

Add simple logging early.

Useful logs:

- Startup sequence.
- Config loading.
- File open/save errors.
- AI connection errors.
- Build command output.

---

## First Milestone

The first major milestone should be intentionally small.

### Milestone 1 Deliverable

A Windows x64 SDL application that:

- Opens a desktop window.
- Displays an 80x25 green monochrome virtual screen.
- Shows a menu bar.
- Shows a status bar.
- Shows a blinking block cursor.
- Allows the user to type text.
- Allows cursor movement.
- Saves a file with F2.
- Opens a file with F3.
- Prompts before exit if the file is modified.

This milestone proves the foundation without overbuilding.

---

## Long-Term Vision

RetroEdit can eventually become a complete retro-modern development environment.

Possible future features:

- Full code editor.
- Project system.
- Compiler integration.
- Build output panel.
- Error navigation.
- Syntax highlighting.
- Local AI assistant.
- Serial terminal mode.
- Macro recording.
- Scriptable commands.
- Plug-in support.
- Retro documentation viewer.
- Integrated help system.

The guiding principle should remain:

```text
Modern capability, early-1980s interaction style.
```
