#include "ScreenBuffer.h"

ScreenBuffer::ScreenBuffer(int columns, int rows)
    : m_columns(columns), m_rows(rows), m_cells(columns * rows)
{
}

void ScreenBuffer::Clear(const ScreenCell& fillCell)
{
    for (auto& cell : m_cells)
        cell = fillCell;
}

void ScreenBuffer::PutChar(int x, int y, char32_t ch, Color fg, Color bg)
{
    if (x < 0 || x >= m_columns || y < 0 || y >= m_rows)
        return;

    ScreenCell& cell  = m_cells[y * m_columns + x];
    cell.character    = ch;
    cell.foreground   = fg;
    cell.background   = bg;
    cell.reverseVideo = false;
}

void ScreenBuffer::WriteText(int x, int y, const std::string& text, Color fg, Color bg)
{
    int col = x;
    for (unsigned char ch : text)
    {
        if (col >= m_columns) break;
        PutChar(col++, y, static_cast<char32_t>(ch), fg, bg);
    }
}

const ScreenCell& ScreenBuffer::At(int x, int y) const
{
    return m_cells[y * m_columns + x];
}

ScreenCell& ScreenBuffer::At(int x, int y)
{
    return m_cells[y * m_columns + x];
}
