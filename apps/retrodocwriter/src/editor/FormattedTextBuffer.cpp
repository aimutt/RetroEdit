#include "FormattedTextBuffer.h"
#include <algorithm>

FormattedTextBuffer::FormattedTextBuffer()
{
    // TextBuffer default ctor leaves one empty line. Mirror that with one
    // empty format row so the invariant (lineCount matches across both
    // layers) holds from the start.
    m_formats.emplace_back();
}

CharFormat FormattedTextBuffer::FormatAt(int row, int col) const
{
    if (row < 0 || row >= static_cast<int>(m_formats.size())) return {};
    const auto& fmtRow = m_formats[row];
    if (col < 0 || col >= static_cast<int>(fmtRow.size())) return {};
    return fmtRow[col];
}

bool FormattedTextBuffer::HasAnyFormatting() const
{
    for (const auto& row : m_formats)
        for (const auto& f : row)
            if (!f.IsPlain()) return true;
    return false;
}

void FormattedTextBuffer::SetLines(std::vector<std::string> lines,
                                   std::vector<std::vector<CharFormat>> formats)
{
    if (lines.empty())
        lines.emplace_back();
    // Pad formats to match lines exactly. Any size mismatch (e.g. truncated
    // sidecar / RTF parser bug) results in missing entries treated as
    // default CharFormat (no overrides).
    while (formats.size() < lines.size())
        formats.emplace_back();
    formats.resize(lines.size());
    for (size_t i = 0; i < lines.size(); ++i)
        formats[i].resize(lines[i].size(), CharFormat{});

    m_text.SetLines(std::move(lines));
    m_formats = std::move(formats);
}

void FormattedTextBuffer::SetLinesPlain(std::vector<std::string> lines)
{
    if (lines.empty())
        lines.emplace_back();
    m_formats.clear();
    m_formats.resize(lines.size());
    for (size_t i = 0; i < lines.size(); ++i)
        m_formats[i].assign(lines[i].size(), CharFormat{});
    m_text.SetLines(std::move(lines));
}

void FormattedTextBuffer::EnsureLineFormatSize(int row, size_t len)
{
    if (row < 0 || row >= static_cast<int>(m_formats.size())) return;
    if (m_formats[row].size() < len)
        m_formats[row].resize(len, CharFormat{});
}

// ---------------------------------------------------------------------------
// Mutators — every text-layer change is mirrored to m_formats in the same call.
// ---------------------------------------------------------------------------

void FormattedTextBuffer::InsertChar(int col, int row, char ch, CharFormat fmt)
{
    if (row < 0 || row >= m_text.LineCount()) return;
    int len = m_text.LineLength(row);
    col = std::clamp(col, 0, len);

    m_text.InsertChar(col, row, ch);
    EnsureLineFormatSize(row, static_cast<size_t>(len));
    m_formats[row].insert(m_formats[row].begin() + col, fmt);
}

void FormattedTextBuffer::Backspace(int col, int row)
{
    if (row < 0 || row >= m_text.LineCount()) return;

    if (col > 0)
    {
        col = std::min(col, m_text.LineLength(row));
        m_text.Backspace(col, row);
        if (col - 1 < static_cast<int>(m_formats[row].size()))
            m_formats[row].erase(m_formats[row].begin() + (col - 1));
    }
    else if (row > 0)
    {
        m_text.Backspace(col, row);
        // Lines merged: append row's formats onto row-1, then erase row.
        auto tail = std::move(m_formats[row]);
        m_formats[row - 1].insert(m_formats[row - 1].end(), tail.begin(), tail.end());
        m_formats.erase(m_formats.begin() + row);
    }
}

void FormattedTextBuffer::DeleteForward(int col, int row)
{
    if (row < 0 || row >= m_text.LineCount()) return;
    int len = m_text.LineLength(row);

    if (col < len)
    {
        m_text.DeleteForward(col, row);
        if (col < static_cast<int>(m_formats[row].size()))
            m_formats[row].erase(m_formats[row].begin() + col);
    }
    else if (row < m_text.LineCount() - 1)
    {
        m_text.DeleteForward(col, row);
        auto tail = std::move(m_formats[row + 1]);
        m_formats[row].insert(m_formats[row].end(), tail.begin(), tail.end());
        m_formats.erase(m_formats.begin() + row + 1);
    }
}

void FormattedTextBuffer::InsertNewline(int col, int row)
{
    if (row < 0 || row >= m_text.LineCount()) return;
    int len = m_text.LineLength(row);
    col = std::clamp(col, 0, len);

    m_text.InsertNewline(col, row);
    EnsureLineFormatSize(row, static_cast<size_t>(len));

    std::vector<CharFormat> tail(m_formats[row].begin() + col, m_formats[row].end());
    m_formats[row].resize(col);
    m_formats.insert(m_formats.begin() + row + 1, std::move(tail));
}

void FormattedTextBuffer::InsertText(int col, int row, const std::string& text,
                                     CharFormat fmt,
                                     int& outEndRow, int& outEndCol)
{
    if (row < 0 || row >= m_text.LineCount())
    {
        outEndRow = row;
        outEndCol = col;
        return;
    }

    m_text.InsertText(col, row, text, outEndRow, outEndCol);

    int len = m_text.LineLength(row);
    EnsureLineFormatSize(row, static_cast<size_t>(len));

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
        m_formats[row].insert(m_formats[row].begin() + col,
                              parts[0].size(), fmt);
        return;
    }

    std::vector<CharFormat> origRowFormats = std::move(m_formats[row]);
    size_t headLen = static_cast<size_t>(col);
    if (headLen > origRowFormats.size()) headLen = origRowFormats.size();

    std::vector<CharFormat> firstRow(origRowFormats.begin(),
                                     origRowFormats.begin() + headLen);
    firstRow.insert(firstRow.end(), parts[0].size(), fmt);
    m_formats[row] = std::move(firstRow);

    int insertAt = row + 1;
    for (size_t i = 1; i + 1 < parts.size(); ++i)
    {
        std::vector<CharFormat> midRow(parts[i].size(), fmt);
        m_formats.insert(m_formats.begin() + insertAt++, std::move(midRow));
    }

    std::vector<CharFormat> lastRow(parts.back().size(), fmt);
    if (headLen < origRowFormats.size())
        lastRow.insert(lastRow.end(),
                       origRowFormats.begin() + headLen,
                       origRowFormats.end());
    m_formats.insert(m_formats.begin() + insertAt, std::move(lastRow));
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

    EnsureLineFormatSize(startRow, static_cast<size_t>(startCol));
    EnsureLineFormatSize(endRow,   static_cast<size_t>(endCol));

    if (startRow == endRow)
    {
        m_formats[startRow].erase(m_formats[startRow].begin() + startCol,
                                  m_formats[startRow].begin() + endCol);
        return;
    }

    std::vector<CharFormat> head(m_formats[startRow].begin(),
                                 m_formats[startRow].begin() + startCol);
    std::vector<CharFormat> tail(m_formats[endRow].begin() + endCol,
                                 m_formats[endRow].end());
    m_formats.erase(m_formats.begin() + startRow + 1,
                    m_formats.begin() + endRow + 1);
    head.insert(head.end(), tail.begin(), tail.end());
    m_formats[startRow] = std::move(head);
}

// ---------------------------------------------------------------------------
// Style/face/size range ops
// ---------------------------------------------------------------------------

bool FormattedTextBuffer::AllInRangeHaveStyle(int startRow, int startCol,
                                              int endRow,   int endCol,
                                              uint8_t bits) const
{
    if (bits == 0) return false;
    if (startRow > endRow || (startRow == endRow && startCol >= endCol)) return false;
    if (startRow < 0 || endRow >= static_cast<int>(m_formats.size())) return false;

    for (int r = startRow; r <= endRow; ++r)
    {
        int rowLen = static_cast<int>(m_formats[r].size());
        int cStart = (r == startRow) ? startCol : 0;
        int cEnd   = (r == endRow)   ? endCol   : rowLen;
        for (int c = cStart; c < cEnd; ++c)
        {
            uint8_t s = (c < rowLen) ? m_formats[r][c].style : 0;
            if ((s & bits) != bits) return false;
        }
    }
    return true;
}

namespace
{
    template <typename Mutator>
    void ApplyAcrossRange(std::vector<std::vector<CharFormat>>& fmt,
                          const TextBuffer& text,
                          int startRow, int startCol,
                          int endRow,   int endCol,
                          Mutator mut)
    {
        if (startRow > endRow || (startRow == endRow && startCol >= endCol)) return;
        if (startRow < 0 || endRow >= static_cast<int>(fmt.size())) return;
        for (int r = startRow; r <= endRow; ++r)
        {
            int textLen = text.LineLength(r);
            if (static_cast<int>(fmt[r].size()) < textLen)
                fmt[r].resize(textLen, CharFormat{});
            int cStart = (r == startRow) ? startCol : 0;
            int cEnd   = (r == endRow)   ? endCol   : textLen;
            cEnd = std::min(cEnd, textLen);
            for (int c = cStart; c < cEnd; ++c)
                mut(fmt[r][c]);
        }
    }
}

void FormattedTextBuffer::SetStyleInRange(int startRow, int startCol,
                                          int endRow,   int endCol,
                                          uint8_t bits, bool on)
{
    if (bits == 0) return;
    ApplyAcrossRange(m_formats, m_text, startRow, startCol, endRow, endCol,
                     [bits, on](CharFormat& f) {
                         if (on) f.style |=  bits;
                         else    f.style &= ~bits;
                     });
}

void FormattedTextBuffer::SetFaceInRange(int startRow, int startCol,
                                         int endRow,   int endCol,
                                         uint8_t faceIdx)
{
    ApplyAcrossRange(m_formats, m_text, startRow, startCol, endRow, endCol,
                     [faceIdx](CharFormat& f) { f.face = faceIdx; });
}

void FormattedTextBuffer::SetSizeInRange(int startRow, int startCol,
                                         int endRow,   int endCol,
                                         uint8_t sizeIdx)
{
    ApplyAcrossRange(m_formats, m_text, startRow, startCol, endRow, endCol,
                     [sizeIdx](CharFormat& f) { f.size = sizeIdx; });
}

void FormattedTextBuffer::FlattenAllStyles()
{
    for (auto& row : m_formats)
        std::fill(row.begin(), row.end(), CharFormat{});
}
