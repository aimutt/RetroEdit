#pragma once

struct Selection
{
    bool active    = false;
    int  anchorRow = 0;
    int  anchorCol = 0;

    void Activate(int cursorRow, int cursorCol)
    {
        anchorRow = cursorRow;
        anchorCol = cursorCol;
        active    = true;
    }

    void Clear() { active = false; }

    bool IsEmpty(int cursorRow, int cursorCol) const
    {
        return anchorRow == cursorRow && anchorCol == cursorCol;
    }

    // Returns the selection as a normalized [start, end) range.
    // end is exclusive: the character AT endCol on endRow is NOT selected.
    void GetRange(int cursorRow, int cursorCol,
                  int& startRow, int& startCol,
                  int& endRow,   int& endCol) const
    {
        bool anchorFirst = (anchorRow < cursorRow) ||
                           (anchorRow == cursorRow && anchorCol <= cursorCol);
        if (anchorFirst)
        {
            startRow = anchorRow; startCol = anchorCol;
            endRow   = cursorRow; endCol   = cursorCol;
        }
        else
        {
            startRow = cursorRow; startCol = cursorCol;
            endRow   = anchorRow; endCol   = anchorCol;
        }
    }

    // Returns true if the cell at (col, line) is inside the selection.
    bool ContainsCell(int col, int line, int cursorRow, int cursorCol) const
    {
        if (!active || IsEmpty(cursorRow, cursorCol)) return false;

        int startRow, startCol, endRow, endCol;
        GetRange(cursorRow, cursorCol, startRow, startCol, endRow, endCol);

        if (line < startRow || line > endRow)        return false;
        if (line == startRow && col < startCol)      return false;
        if (line == endRow   && col >= endCol)       return false;
        return true;
    }
};
