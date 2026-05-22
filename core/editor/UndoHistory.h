#pragma once
#include "TextBuffer.h"
#include <vector>
#include <string>

struct UndoState
{
    std::vector<std::string> lines;
    int cursorRow = 0;
    int cursorCol = 0;
};

class UndoHistory
{
public:
    static constexpr int MAX_DEPTH = 200;

    // Call BEFORE making an edit. Saves current state, clears redo stack.
    void PushEdit(const TextBuffer& buf, int cursorRow, int cursorCol);

    bool CanUndo() const { return !m_undoStack.empty(); }
    bool CanRedo() const { return !m_redoStack.empty(); }

    // Each returns the state to restore; saves current state on the opposite stack.
    UndoState Undo(const TextBuffer& buf, int cursorRow, int cursorCol);
    UndoState Redo(const TextBuffer& buf, int cursorRow, int cursorCol);

    void ClearAll();

private:
    std::vector<UndoState> m_undoStack;
    std::vector<UndoState> m_redoStack;

    static UndoState Snapshot(const TextBuffer& buf, int row, int col);
};
