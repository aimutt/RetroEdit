# RetroDocWriter Project Plan

> **RetroDocWriter is the WYSIWYG document-writer sibling of [RetroEdit](RetroEdit.md).** Same retro green-screen look and feel, same monorepo, but a distinct product with a separate EXE and a different product surface. The two products share a large core via the `RetroCore` static library (see [/CLAUDE.md](../CLAUDE.md)).

## Why a second product

RetroEdit is a plain-text editor — one buffer of `std::string` lines, no per-character formatting, character-cell rendering. Pushing WYSIWYG (proportional layout, page margins, per-character bold/italic/font runs) onto that same `Application` and menu surface was making both products worse. The split lets each product do **one thing well**:

- **RetroEdit** — pure plain-text editor. Always character-cell rendering. No pages, no margins, no formatting.
- **RetroDocWriter** — always-on WYSIWYG document writer. Proportional layout on a US Letter page, per-character bold/italic/underline/strikethrough, native RTF I/O.

## Current state (Phase 1 + Phase 2 shipped)

**Phase 1 — layout-only WYSIWYG:**

- Proportional text rendering via `WysiwygRenderer` (`apps/retrodocwriter/src/render/WysiwygRenderer.cpp`)
- US Letter page (8.5″ × 11″) centered in the editor area
- Per-document margins (Page > Margins…), persisted to the `.retroedit` sidecar
- DPI-independent `ComputeCharsPerLine` so the on-screen wrap matches what the printer produces
- Print integration (Print uses the document font and the same chars-per-line as on screen)
- Cursor + selection in proportional layout, with vertical scrolling

**Phase 2 — per-character formatting:**

- Bold / Italic / Underline / Strikethrough applied at the character level, rendered via SDL3_ttf's `TTF_SetFontStyle` synthetic styles (no extra font files needed)
- Format menu (Alt+R) with `Bold (Ctrl+B)`, `Italic (Ctrl+I)`, `Underline (Ctrl+U)`, `Strikethrough` (menu-only)
- Smart toggle: with a selection, the action XOR-toggles the bit across the range (clears if all selected chars have it, sets otherwise). Without a selection, the action toggles the bit for next-typed input.
- Status-bar `B I U S` indicators show the next-typed style (highlighted when active)
- Native file format: **`.rtf`** (a minimal documented subset — see [RTF subset reference](#rtf-subset-reference) below). Reads files written by Word, WordPad, LibreOffice; produces files those programs read.
- Plain `.txt` files still open and edit normally. If formatting is added and the user hits Ctrl+S, a `Save Formatted Document` confirm dialog asks whether to Save As .rtf (preserves formatting) or save plain .txt (flatten and discard).
- Undo/redo restores both text and styles together (separate `RichUndoHistory` keyed off `FormattedTextBuffer` snapshots)

**Explicitly not in Phase 2:**

- Per-run font face / point size (defer to Phase 3 — the data model is ready, the renderer is not)
- Color / highlighting
- Page breaks (text still flows as one tall page)
- Rulers, margin guides, headers/footers
- Print preview pane
- Selectable page size (US Letter is hardcoded)
- **Print path is style-blind for now** — Print menu still uses the document font uniformly. Formatted documents print without bold/italic/etc. Phase 3 work.

## Architecture (where things live)

```
core/                                    shared via RetroCore.lib
  editor/TextBuffer.*                    plain-text buffer; both products use this
  editor/UndoHistory.*                   snapshot-based undo (plain-text only — RetroEdit)
  editor/FileDocument.*                  plain-text load/save (RetroEdit uses this)
  editor/FileSettings.*                  .retroedit sidecar (key/value, round-trips unknowns)
  render/RetroRenderer.*                 cell-grid SDL renderer
  render/GlyphCache.*                    glyph atlas, keyed on (codepoint, styleBits)
  render/Theme.h                         retro green palette
  platform/Print.*                       GDI print integration (Windows)

apps/retrodocwriter/src/
  editor/CharStyle.h                     style bitmask (Bold/Italic/Underline/Strikethrough)
  editor/FormattedTextBuffer.*           TextBuffer + parallel per-char style vector
  editor/RichUndoHistory.*               undo/redo capturing both text and styles
  editor/RichFileDocument.*              load/save dispatcher: .rtf vs plain text
  editor/RtfWriter.*                     serialize FormattedTextBuffer to RTF subset
  editor/RtfReader.*                     parse RTF subset into FormattedTextBuffer
  app/Application.*                      WYSIWYG-default Application (m_wysiwygEnabled = true)
  render/WysiwygRenderer.*               proportional layout, page rendering, per-char style
  ui/MenuDefs.h                          File / Edit / Format / Search / View / Page / Tools / Options / Help
  ui/RetroUi.*                           Menu + dialogs + status-bar B/I/U/S indicators
```

`Application` is currently **duplicated** between RetroEdit and RetroDocWriter. The duplication is intentional until Stage 5 (CommandRegistry / EventBus) gives us a natural place to extract a shared base. Until then, small reusable helpers belong in `core/app/`, and each product's `Application` is free to diverge for product-specific behaviors.

## RTF subset reference

RetroDocWriter writes (and is guaranteed to round-trip) the following RTF control words. Everything else is **skipped** on read and **not emitted** on write.

| Control word | Meaning                                                |
|--------------|--------------------------------------------------------|
| `\rtf1`      | RTF version marker (header)                            |
| `\ansi`      | Default character set                                  |
| `\ansicpg1252` | Default code page                                    |
| `\fonttbl`   | Font table group (single entry, the document font)     |
| `\f0`        | Selects font 0                                         |
| `\deff0`     | Default font index                                     |
| `\fs<N>`     | Font size in half-points (`\fs24` = 12 pt)             |
| `\b` / `\b0` | Bold on / off                                          |
| `\i` / `\i0` | Italic on / off                                        |
| `\ul` / `\ulnone` | Underline on / off (also accepts `\ul0`)          |
| `\strike` / `\strike0` | Strikethrough on / off                       |
| `\par`       | Paragraph break (= newline)                            |
| `\line`      | Soft line break (read only; treated as newline)        |
| `\\`, `\{`, `\}` | Literal backslash / open brace / close brace       |
| `\'hh`       | Hex byte escape (for high-bit Latin-1 characters)      |
| `{` / `}`    | Group delimiters — `{` saves style; `}` restores it    |

**Read-only skip-list:** RetroDocWriter recognizes the following destination groups and silently consumes them (text inside is not added to the document body):

```
fonttbl  filetbl  colortbl  stylesheet  listtable  rsidtbl
info     pict     header    footer      headerl    headerr
footerl  footerr  themedata datastore   object     field
shppict  nonshppict bkmkstart bkmkend   xe         tc
```

Plus the standard `\*` "ignorable destination" introducer, which marks the *next* group as one to skip whatever its destination keyword.

Files written outside this subset (e.g. Word with embedded images, tables, or paragraph styles) still **open** in RetroDocWriter — the body text plus our four style bits comes through, and everything else is dropped on load. **Saving** such a file from RetroDocWriter will not preserve the dropped content; users opening a Word document with rich features in RetroDocWriter should treat it as a one-way conversion to the supported subset.

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

Note that **RTF formatting itself is not stored in the sidecar.** It lives inside the `.rtf` file body. The sidecar only carries view/editor state.

## Out of scope (do not add to RetroDocWriter)

- Anything that belongs in RetroEdit (the plain-text product). If a feature is useful for plain text, it goes in `core/` or in `apps/retroedit/` — not here.
- A new `ApplicationBase` class hierarchy. Wait for Stage 5.
- Wider RTF spec coverage than the documented subset above. Adding control words is fine; adding tables, footnotes, or embedded images is a separate, much larger project.
