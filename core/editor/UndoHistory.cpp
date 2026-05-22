#include "UndoHistory.h"

UndoState UndoHistory::Snapshot(const TextBuffer& buf, int row, int col)
{
    UndoState s;
    s.cursorRow = row;
    s.cursorCol = col;
    s.lines.reserve(static_cast<size_t>(buf.LineCount()));
    for (int i = 0; i < buf.LineCount(); ++i)
        s.lines.push_back(buf.Line(i));
    return s;
}

void UndoHistory::PushEdit(const TextBuffer& buf, int cursorRow, int cursorCol)
{
    m_redoStack.clear();
    m_undoStack.push_back(Snapshot(buf, cursorRow, cursorCol));
    if (static_cast<int>(m_undoStack.size()) > MAX_DEPTH)
        m_undoStack.erase(m_undoStack.begin());
}

UndoState UndoHistory::Undo(const TextBuffer& buf, int cursorRow, int cursorCol)
{
    m_redoStack.push_back(Snapshot(buf, cursorRow, cursorCol));
    UndoState s = std::move(m_undoStack.back());
    m_undoStack.pop_back();
    return s;
}

UndoState UndoHistory::Redo(const TextBuffer& buf, int cursorRow, int cursorCol)
{
    m_undoStack.push_back(Snapshot(buf, cursorRow, cursorCol));
    UndoState s = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    return s;
}

void UndoHistory::ClearAll()
{
    m_undoStack.clear();
    m_redoStack.clear();
}
