# RetroDocWriter Project Plan

> **RetroDocWriter is the WYSIWYG document-writer sibling of [RetroEdit](RetroEdit.md).** Same retro green-screen look and feel, same monorepo, but a distinct product with a separate EXE and a different product surface. The two products share roughly 80% of their code via the `RetroCore` static library (see [/CLAUDE.md](../CLAUDE.md)).

## Why a second product

RetroEdit is a plain-text editor — one buffer of `std::string` lines, no per-character formatting, character-cell rendering. Pushing WYSIWYG (proportional layout, page margins, eventual per-character bold/italic/font runs) onto that same `Application` and menu surface was making both products worse. The split lets each product do **one thing well**:

- **RetroEdit** — pure plain-text editor. Always character-cell rendering. No pages, no margins, no formatting.
- **RetroDocWriter** — always-on WYSIWYG document writer. Proportional layout on a US Letter page. Future home for rich-text editing.

## Current state (Phase 1 WYSIWYG)

What RetroDocWriter ships today is **layout-only WYSIWYG**:

- Proportional text rendering via `WysiwygRenderer` (`apps/retrodocwriter/src/render/WysiwygRenderer.cpp`)
- US Letter page (8.5″ × 11″) centered in the editor area
- Per-document margins (Page > Margins…), persisted to the `.retroedit` sidecar
- DPI-independent `ComputeCharsPerLine` so the on-screen wrap matches what the printer produces
- Print integration (Print uses the document font and the same chars-per-line as on screen)
- Cursor + selection in proportional layout, with vertical scrolling

What is **not** in Phase 1:

- Per-character formatting (bold, italic, font size, font face changes mid-document)
- Page breaks (text flows as one tall page)
- Rulers, margin guides, headers/footers
- Print preview pane
- Selectable page size (US Letter is hardcoded)

The underlying text storage is still plain `std::string` lines in the shared `TextBuffer` (`core/editor/TextBuffer.h`). Phase 2 is where the rich-text data model lands.

## Phase 2: per-character formatting (planned)

Phase 2 introduces formatting attributes attached to character runs and the UI to apply them. Sketch:

- New `RichTextBuffer` (or `FormattedRun` overlay on top of `TextBuffer`) in `apps/retrodocwriter/src/editor/` — **not** in `core/`, since RetroEdit stays unaware of formatting.
- Bold / italic / underline as the first attributes; font-face and point-size runs after.
- File format: a sidecar (`<file>.retrodoc`) or an extension to the existing `.retroedit` sidecar — TBD when Phase 2 starts.
- Toolbar / shortcut keys for the formatting commands (Ctrl+B / Ctrl+I / Ctrl+U). Live in the WYSIWYG product only.

Phase 2 should not require changes to `RetroCore` beyond possibly extending `Selection` (which already lives in `core/editor/`) and the print path (`core/platform/Print.cpp`).

## Architecture (where things live)

```
core/                                    shared via RetroCore.lib
  editor/TextBuffer.*                    plain-text buffer; both products use this
  editor/UndoHistory.*                   snapshot-based undo
  editor/FileSettings.*                  .retroedit sidecar (key/value, round-trips unknowns)
  render/RetroRenderer.*                 cell-grid SDL renderer
  render/GlyphCache.*                    glyph atlas, used by both renderers
  render/Theme.h                         retro green palette
  platform/Print.*                       GDI print integration (Windows)

apps/retrodocwriter/src/
  app/Application.*                      WYSIWYG-default Application (defaults m_wysiwygEnabled = true)
  render/WysiwygRenderer.*               proportional layout, page rendering, margin guides
  ui/MenuDefs.h                          File / Edit / Search / View / Page / Tools / Options / Help
  ui/RetroUi.*                           Menu + dialogs, including Margins dialog
```

`Application` is currently **duplicated** between RetroEdit and RetroDocWriter. The duplication is intentional until Stage 5 (CommandRegistry / EventBus) gives us a natural place to extract a shared base. Until then, small reusable helpers belong in `core/app/`, and each product's `Application` is free to diverge for product-specific behaviors.

## Build

The top-level `cmake --build build --config Debug` produces both EXEs. The RetroDocWriter EXE lives at `build/apps/retrodocwriter/Debug/RetroDocWriter.exe` with `SDL3.dll`, `SDL3_ttf.dll`, and the `assets/` directory copied beside it (handled by the per-app `POST_BUILD` step).

## Sidecar file compatibility

A `.retroedit` sidecar written by RetroDocWriter looks like:

```
word_wrap=false
show_word_count=false
wysiwyg=true
margin_top=1.00
margin_bottom=1.00
margin_left=1.00
margin_right=1.00
```

If the same file is opened in RetroEdit:

- RetroEdit reads `word_wrap` and `show_word_count` (the keys it knows).
- RetroEdit silently ignores `wysiwyg`, `margin_*` (unknown keys to it).
- When RetroEdit saves the sidecar, it **loads the existing file first** so unknown keys round-trip untouched — the WYSIWYG state is preserved for the next time RetroDocWriter opens the same file.

This works because `FileSettings` is a generic key/value store; see `core/editor/FileSettings.h`.

## Out of scope (do not add to RetroDocWriter)

- Anything that belongs in RetroEdit (the plain-text product). If a feature is useful for plain text, it goes in `core/` or in `apps/retroedit/` — not here.
- A new `ApplicationBase` class hierarchy. Wait for Stage 5.
- A bespoke rich-text file format before Phase 2 lands. Until then, RetroDocWriter shares the plain-text format with RetroEdit so the two products can interoperate on the same documents.
