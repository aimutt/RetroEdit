#include "FormattedTextBuffer.h"
#include <algorithm>

FormattedTextBuffer::FormattedTextBuffer()
{
    // TextBuffer default ctor leaves one empty line. Mirror that with one
    // empty style row so the invariant (lineCount matches across both
    // layers) holds from the start.
    m_styles.emplace_back();
}

uint8_t FormattedTextBuffer::StyleAt(int row, int col) const
{
    if (row < 0 || row >= static_cast<int>(m_styles.size())) return 0;
    const auto& styleRow = m_styles[row];
    if (col < 0 || col >= static_cast<int>(styleRow.size())) return 0;
    return styleRow[col];
}

bool FormattedTextBuffer::HasAnyFormatting() const
{
    for (const auto& row : m_styles)
        for (uint8_t b : row)
            if (b != 0) return true;
    return false;
}

void FormattedTextBuffer::SetLines(std::vector<std::string> lines,
                                   std::vector<std::vector<uint8_t>> styles)
{
    if (lines.empty())
        lines.emplace_back();
    // Pad styles to match lines exactly. Any size mismatch (e.g. truncated
    // sidecar / RTF parser bug) results in missing entries treated as 0.
    while (styles.size() < lines.size())
        styles.emplace_back();
    styles.resize(lines.size());
    for (size_t i = 0; i < lines.size(); ++i)
        styles[i].resize(lines[i].size(), 0);

    m_text.SetLines(std::move(lines));
    m_styles = std::move(styles);
}

void FormattedTextBuffer::SetLinesPlain(std::vector<std::string> lines)
{
    if (lines.empty())
        lines.emplace_back();
    m_styles.clear();
    m_styles.resize(lines.size());
    for (size_t i = 0; i < lines.size(); ++i)
        m_styles[i].assign(lines[i].size(), 0);
    m_text.SetLines(std::move(lines));
}

void FormattedTextBuffer::EnsureLineStyleSize(int row, size_t len)
{
    if (row < 0 || row >= static_cast<int>(m_styles.size())) return;
    if (m_styles[row].size() < len)
        m_styles[row].resize(len, 0);
}

// ---------------------------------------------------------------------------
// Mutators — every text-layer change is mirrored to m_styles in the same call.
// ---------------------------------------------------------------------------

void FormattedTextBuffer::InsertChar(int col, int row, char ch, uint8_t style)
{
    if (row < 0 || row >= m_text.LineCount()) return;
    int len = m_text.LineLength(row);
    col = std::clamp(col, 0, len);

    m_text.InsertChar(col, row, ch);
    EnsureLineStyleSize(row, static_cast<size_t>(len));
    m_styles[row].insert(m_styles[row].begin() + col, style);
}

void FormattedTextBuffer::Backspace(int col, int row)
{
    if (row < 0 || row >= m_text.LineCount()) return;

    if (col > 0)
    {
        col = std::min(col, m_text.LineLength(row));
        m_text.Backspace(col, row);
        // Erase the style at col-1 to match the byte that was removed.
        if (col - 1 < static_cast<int>(m_styles[row].size()))
            m_styles[row].erase(m_styles[row].begin() + (col - 1));
    }
    else if (row > 0)
    {
        m_text.Backspace(col, row);
        // Lines merged: append row's styles onto row-1, then erase row.
        auto tail = std::move(m_styles[row]);
        m_styles[row - 1].insert(m_styles[row - 1].end(), tail.begin(), tail.end());
        m_styles.erase(m_styles.begin() + row);
    }
}

void FormattedTextBuffer::DeleteForward(int col, int row)
{
    if (row < 0 || row >= m_text.LineCount()) return;
    int len = m_text.LineLength(row);

    if (col < len)
    {
        m_text.DeleteForward(col, row);
        if (col < static_cast<int>(m_styles[row].size()))
            m_styles[row].erase(m_styles[row].begin() + col);
    }
    else if (row < m_text.LineCount() - 1)
    {
        m_text.DeleteForward(col, row);
        // Lines merged: append next-row styles onto this row, erase next.
        auto tail = std::move(m_styles[row + 1]);
        m_styles[row].insert(m_styles[row].end(), tail.begin(), tail.end());
        m_styles.erase(m_styles.begin() + row + 1);
    }
}

void FormattedTextBuffer::InsertNewline(int col, int row)
{
    if (row < 0 || row >= m_text.LineCount()) return;
    int len = m_text.LineLength(row);
    col = std::clamp(col, 0, len);

    m_text.InsertNewline(col, row);
    EnsureLineStyleSize(row, static_cast<size_t>(len));

    // Split this row's styles at col: head stays, tail becomes new row.
    std::vector<uint8_t> tail(m_styles[row].begin() + col, m_styles[row].end());
    m_styles[row].resize(col);
    m_styles.insert(m_styles.begin() + row + 1, std::move(tail));
}

void FormattedTextBuffer::InsertText(int col, int row, const std::string& text,
                                     uint8_t style,
                                     int& outEndRow, int& outEndCol)
{
    if (row < 0 || row >= m_text.LineCount())
    {
        outEndRow = row;
        outEndCol = col;
        return;
    }

    m_text.InsertText(col, row, text, outEndRow, outEndCol);

    // Rebuild m_styles for the affected span by walking the freshly inserted
    // text and re-emitting style bytes line-by-line. The inserted text uses
    // the single passed-in style for every character.
    int len = m_text.LineLength(row);
    EnsureLineStyleSize(row, static_cast<size_t>(len));

    // Recover the original split: m_text already merged everything, so we
    // reconstruct by replaying the parse-by-'\n' that TextBuffer used.
    std::vector<std::string> parts;
    std::string cur;
    for (char ch : text)
    {
        if (ch == '\n') { parts.push_back(std::move(cur)); cur.clear(); }
        else if (ch != '\r') cur += ch;
    }
    parts.push_back(std::move(cur));

    if (parts.size() == 1)
    {
        // Single-line insert: splice `parts[0].size()` style bytes in at col.
        m_styles[row].insert(m_styles[row].begin() + col,
                             parts[0].size(), style);
        return;
    }

    // Multi-line: m_styles[row] currently holds the entire row's styles up
    // to the merge with parts.back() + original tail. Rebuild by splitting.
    std::vector<uint8_t> origRowStyles = std::move(m_styles[row]);
    size_t headLen = static_cast<size_t>(col);
    if (headLen > origRowStyles.size()) headLen = origRowStyles.size();

    // Row[row] = head + parts[0] (all with new style).
    std::vector<uint8_t> firstRow(origRowStyles.begin(),
                                  origRowStyles.begin() + headLen);
    firstRow.insert(firstRow.end(), parts[0].size(), style);
    m_styles[row] = std::move(firstRow);

    // Intermediate rows: entirely new content with new style.
    int insertAt = row + 1;
    for (size_t i = 1; i + 1 < parts.size(); ++i)
    {
        std::vector<uint8_t> midRow(parts[i].size(), style);
        m_styles.insert(m_styles.begin() + insertAt++, std::move(midRow));
    }

    // Last row: parts.back() (new style) + original tail (preserved styles).
    std::vector<uint8_t> lastRow(parts.back().size(), style);
    if (headLen < origRowStyles.size())
        lastRow.insert(lastRow.end(),
                       origRowStyles.begin() + headLen,
                       origRowStyles.end());
    m_styles.insert(m_styles.begin() + insertAt, std::move(lastRow));
}

void FormattedTextBuffer::DeleteRange(int startRow, int startCol,
                                      int endRow,   int endCol)
{
    if (m_text.LineCount() == 0) return;
    startRow = std::clamp(startRow, 0, m_text.LineCount() - 1);
    endRow   = std::clamp(endRow,   0, m_text.LineCount() - 1);
    startCol = std::clamp(startCol, 0, m_text.LineLength(startRow));
    endCol   = std::clamp(endCol,   0, m_text.LineLength(endRow));

    m_text.DeleteRange(startRow, startCol, endRow, endCol);

    EnsureLineStyleSize(startRow, static_cast<size_t>(startCol));
    EnsureLineStyleSize(endRow,   static_cast<size_t>(endCol));

    if (startRow == endRow)
    {
        m_styles[startRow].erase(m_styles[startRow].begin() + startCol,
                                 m_styles[startRow].begin() + endCol);
        return;
    }

    std::vector<uint8_t> head(m_styles[startRow].begin(),
                              m_styles[startRow].begin() + startCol);
    std::vector<uint8_t> tail(m_styles[endRow].begin() + endCol,
                              m_styles[endRow].end());
    m_styles.erase(m_styles.begin() + startRow + 1,
                   m_styles.begin() + endRow + 1);
    head.insert(head.end(), tail.begin(), tail.end());
    m_styles[startRow] = std::move(head);
}

// ---------------------------------------------------------------------------
// Style-only ops
// ---------------------------------------------------------------------------

bool FormattedTextBuffer::AllInRangeHaveStyle(int startRow, int startCol,
                                              int endRow,   int endCol,
                                              uint8_t bits) const
{
    if (bits == 0) return false;
    if (startRow > endRow || (startRow == endRow && startCol >= endCol)) return false;
    if (startRow < 0 || endRow >= static_cast<int>(m_styles.size())) return false;

    for (int r = startRow; r <= endRow; ++r)
    {
        int rowLen = static_cast<int>(m_styles[r].size());
        int cStart = (r == startRow) ? startCol : 0;
        int cEnd   = (r == endRow)   ? endCol   : rowLen;
        for (int c = cStart; c < cEnd; ++c)
        {
            uint8_t s = (c < rowLen) ? m_styles[r][c] : 0;
            if ((s & bits) != bits) return false;
        }
    }
    return true;
}

void FormattedTextBuffer::SetStyleInRange(int startRow, int startCol,
                                          int endRow,   int endCol,
                                          uint8_t bits, bool on)
{
    if (bits == 0) return;
    if (startRow > endRow || (startRow == endRow && startCol >= endCol)) return;
    if (startRow < 0 || endRow >= static_cast<int>(m_styles.size())) return;

    for (int r = startRow; r <= endRow; ++r)
    {
        // Pad to the text-layer length so positions inside the selection
        // that are past current m_styles length still get the bit.
        int textLen = m_text.LineLength(r);
        EnsureLineStyleSize(r, static_cast<size_t>(textLen));

        int cStart = (r == startRow) ? startCol : 0;
        int cEnd   = (r == endRow)   ? endCol   : textLen;
        cEnd = std::min(cEnd, textLen);
        for (int c = cStart; c < cEnd; ++c)
        {
            if (on) m_styles[r][c] |=  bits;
            else    m_styles[r][c] &= ~bits;
        }
    }
}

void FormattedTextBuffer::FlattenAllStyles()
{
    for (auto& row : m_styles)
        std::fill(row.begin(), row.end(), 0);
}
