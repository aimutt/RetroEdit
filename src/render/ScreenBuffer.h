#pragma once
#include "Color.h"
#include <string>
#include <vector>

struct ScreenCell
{
    char32_t character  = U' ';
    Color    foreground;
    Color    background;
    bool     bright      = false;
    bool     blink       = false;
    bool     reverseVideo = false;
};

class ScreenBuffer
{
public:
    ScreenBuffer(int columns, int rows);

    void Clear(const ScreenCell& fillCell);
    void PutChar(int x, int y, char32_t ch, Color fg, Color bg);
    void WriteText(int x, int y, const std::string& text, Color fg, Color bg);

    int Columns() const { return m_columns; }
    int Rows()    const { return m_rows; }

    const ScreenCell& At(int x, int y) const;
    ScreenCell&       At(int x, int y);

private:
    int m_columns = 0;
    int m_rows    = 0;
    std::vector<ScreenCell> m_cells;
};
