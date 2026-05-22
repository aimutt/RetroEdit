#include "RichUndoHistory.h"

RichUndoState RichUndoHistory::Snapshot(const FormattedTextBuffer& buf,
                                        int row, int col)
{
    RichUndoState s;
    s.cursorRow = row;
    s.cursorCol = col;
    int n = buf.LineCount();
    s.lines.reserve(static_cast<size_t>(n));
    s.styles.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        s.lines.push_back(buf.Line(i));
        std::vector<uint8_t> row_styles;
        int len = buf.LineLength(i);
        row_styles.reserve(static_cast<size_t>(len));
        for (int c = 0; c < len; ++c)
            row_styles.push_back(buf.StyleAt(i, c));
        s.styles.push_back(std::move(row_styles));
    }
    return s;
}

void RichUndoHistory::PushEdit(const FormattedTextBuffer& buf,
                               int cursorRow, int cursorCol)
{
    m_redoStack.clear();
    m_undoStack.push_back(Snapshot(buf, cursorRow, cursorCol));
    if (static_cast<int>(m_undoStack.size()) > MAX_DEPTH)
        m_undoStack.erase(m_undoStack.begin());
}

RichUndoState RichUndoHistory::Undo(const FormattedTextBuffer& buf,
                                    int cursorRow, int cursorCol)
{
    m_redoStack.push_back(Snapshot(buf, cursorRow, cursorCol));
    RichUndoState s = std::move(m_undoStack.back());
    m_undoStack.pop_back();
    return s;
}

RichUndoState RichUndoHistory::Redo(const FormattedTextBuffer& buf,
                                    int cursorRow, int cursorCol)
{
    m_undoStack.push_back(Snapshot(buf, cursorRow, cursorCol));
    RichUndoState s = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    return s;
}

void RichUndoHistory::ClearAll()
{
    m_undoStack.clear();
    m_redoStack.clear();
}
