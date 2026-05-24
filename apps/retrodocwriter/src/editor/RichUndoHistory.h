#pragma once
#include "FormattedTextBuffer.h"
#include <cstdint>
#include <string>
#include <vector>

// Undo/redo for a FormattedTextBuffer. Mirrors core/editor/UndoHistory's
// API and 200-entry cap, but snapshots both text lines AND the parallel
// CharFormat vectors so style/face/size changes are reversible alongside
// content edits.
//
// RetroEdit continues to use the plain-text UndoHistory in core/. This
// class lives in the RetroDocWriter app only.
struct RichUndoState
{
    std::vector<std::string>                  lines;
    std::vector<std::vector<CharFormat>>      formats;
    std::vector<bool>                         pageBreaks;
    int cursorRow = 0;
    int cursorCol = 0;
};

class RichUndoHistory
{
public:
    static constexpr int MAX_DEPTH = 200;

    void PushEdit(const FormattedTextBuffer& buf, int cursorRow, int cursorCol);

    bool CanUndo() const { return !m_undoStack.empty(); }
    bool CanRedo() const { return !m_redoStack.empty(); }

    RichUndoState Undo(const FormattedTextBuffer& buf, int cursorRow, int cursorCol);
    RichUndoState Redo(const FormattedTextBuffer& buf, int cursorRow, int cursorCol);

    void ClearAll();

private:
    static RichUndoState Snapshot(const FormattedTextBuffer& buf, int row, int col);

    std::vector<RichUndoState> m_undoStack;
    std::vector<RichUndoState> m_redoStack;
};
