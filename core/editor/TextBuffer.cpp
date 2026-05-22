#include "TextBuffer.h"
#include <algorithm>
#include <cctype>

namespace
{
    // ASCII-only case-insensitive substring search. Matches std::string::find
    // semantics: returns the offset of the first match at or after searchFrom,
    // or std::string::npos when none exists.
    size_t FindCaseInsensitive(const std::string& haystack,
                               const std::string& needle,
                               size_t searchFrom)
    {
        const size_t hlen = haystack.size();
        const size_t nlen = needle.size();
        if (nlen == 0 || nlen > hlen) return std::string::npos;
        const size_t last = hlen - nlen;
        for (size_t i = searchFrom; i <= last; ++i)
        {
            bool match = true;
            for (size_t j = 0; j < nlen; ++j)
            {
                unsigned char a = static_cast<unsigned char>(haystack[i + j]);
                unsigned char b = static_cast<unsigned char>(needle[j]);
                if (std::tolower(a) != std::tolower(b)) { match = false; break; }
            }
            if (match) return i;
        }
        return std::string::npos;
    }
}

TextBuffer::TextBuffer()
{
    m_lines.emplace_back();
}

int TextBuffer::LineCount() const
{
    return static_cast<int>(m_lines.size());
}

const std::string& TextBuffer::Line(int lineIndex) const
{
    if (lineIndex < 0 || lineIndex >= LineCount())
    {
        static const std::string empty;
        return empty;
    }
    return m_lines[lineIndex];
}

int TextBuffer::LineLength(int lineIndex) const
{
    return static_cast<int>(Line(lineIndex).size());
}

void TextBuffer::SetLines(std::vector<std::string> lines)
{
    if (lines.empty())
        lines.emplace_back();
    m_lines = std::move(lines);
}

// ---------------------------------------------------------------------------
// Single-character / line edits
// ---------------------------------------------------------------------------

void TextBuffer::InsertChar(int col, int line, char ch)
{
    if (line < 0 || line >= LineCount()) return;
    col = std::clamp(col, 0, LineLength(line));
    m_lines[line].insert(static_cast<std::string::size_type>(col), 1, ch);
}

void TextBuffer::Backspace(int col, int line)
{
    if (line < 0 || line >= LineCount()) return;

    if (col > 0)
    {
        col = std::min(col, LineLength(line));
        m_lines[line].erase(static_cast<std::string::size_type>(col - 1), 1);
    }
    else if (line > 0)
    {
        m_lines[line - 1] += m_lines[line];
        m_lines.erase(m_lines.begin() + line);
    }
}

void TextBuffer::DeleteForward(int col, int line)
{
    if (line < 0 || line >= LineCount()) return;

    if (col < LineLength(line))
        m_lines[line].erase(static_cast<std::string::size_type>(col), 1);
    else if (line < LineCount() - 1)
    {
        m_lines[line] += m_lines[line + 1];
        m_lines.erase(m_lines.begin() + line + 1);
    }
}

void TextBuffer::InsertNewline(int col, int line)
{
    if (line < 0 || line >= LineCount()) return;
    col = std::clamp(col, 0, LineLength(line));
    std::string tail = m_lines[line].substr(static_cast<std::string::size_type>(col));
    m_lines[line].resize(static_cast<std::string::size_type>(col));
    m_lines.insert(m_lines.begin() + line + 1, std::move(tail));
}

// ---------------------------------------------------------------------------
// Block operations
// ---------------------------------------------------------------------------

std::string TextBuffer::GetText(int startRow, int startCol,
                                int endRow,   int endCol) const
{
    startRow = std::clamp(startRow, 0, LineCount() - 1);
    endRow   = std::clamp(endRow,   0, LineCount() - 1);
    startCol = std::clamp(startCol, 0, LineLength(startRow));
    endCol   = std::clamp(endCol,   0, LineLength(endRow));

    if (startRow == endRow)
        return Line(startRow).substr(static_cast<size_t>(startCol),
                                     static_cast<size_t>(endCol - startCol));

    std::string result;
    result += Line(startRow).substr(static_cast<size_t>(startCol));
    result += '\n';
    for (int r = startRow + 1; r < endRow; ++r)
    {
        result += Line(r);
        result += '\n';
    }
    result += Line(endRow).substr(0, static_cast<size_t>(endCol));
    return result;
}

void TextBuffer::DeleteRange(int startRow, int startCol,
                             int endRow,   int endCol)
{
    if (LineCount() == 0) return;
    startRow = std::clamp(startRow, 0, LineCount() - 1);
    endRow   = std::clamp(endRow,   0, LineCount() - 1);
    startCol = std::clamp(startCol, 0, LineLength(startRow));
    endCol   = std::clamp(endCol,   0, LineLength(endRow));

    if (startRow == endRow)
    {
        m_lines[startRow].erase(static_cast<size_t>(startCol),
                                static_cast<size_t>(endCol - startCol));
        return;
    }

    std::string prefix = m_lines[startRow].substr(0, static_cast<size_t>(startCol));
    std::string suffix = m_lines[endRow].substr(static_cast<size_t>(endCol));

    // Erase lines startRow+1 through endRow (inclusive)
    m_lines.erase(m_lines.begin() + startRow + 1,
                  m_lines.begin() + endRow + 1);

    m_lines[startRow] = prefix + suffix;
}

void TextBuffer::InsertText(int col, int line, const std::string& text,
                            int& outEndRow, int& outEndCol)
{
    if (line < 0 || line >= LineCount()) { outEndRow = line; outEndCol = col; return; }
    col = std::clamp(col, 0, LineLength(line));

    // Split text by newlines
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
        m_lines[line].insert(static_cast<size_t>(col), parts[0]);
        outEndRow = line;
        outEndCol = col + static_cast<int>(parts[0].size());
        return;
    }

    // Multi-line paste
    std::string tail = m_lines[line].substr(static_cast<size_t>(col));
    m_lines[line].resize(static_cast<size_t>(col));
    m_lines[line] += parts[0];

    int insertAt = line + 1;
    for (int i = 1; i < static_cast<int>(parts.size()) - 1; ++i)
        m_lines.insert(m_lines.begin() + insertAt++, parts[i]);

    // Last part + original tail
    m_lines.insert(m_lines.begin() + insertAt, parts.back() + tail);

    outEndRow = line + static_cast<int>(parts.size()) - 1;
    outEndCol = static_cast<int>(parts.back().size());
}

bool TextBuffer::FindNext(const std::string& query,
                          int fromRow, int fromCol,
                          int& foundRow, int& foundCol,
                          bool caseInsensitive) const
{
    if (query.empty() || LineCount() == 0) return false;

    int lc = LineCount();
    int qlen = static_cast<int>(query.size());

    // Two-pass: first pass from (fromRow, fromCol+qlen) to end;
    // second pass wraps from row 0 back to fromRow (inclusive).
    for (int pass = 0; pass < 2; ++pass)
    {
        int rStart = (pass == 0) ? fromRow : 0;
        int rEnd   = (pass == 0) ? lc      : fromRow + 1;

        for (int r = rStart; r < rEnd; ++r)
        {
            const std::string& ln = Line(r);
            size_t searchFrom = 0;
            if (pass == 0 && r == fromRow)
                searchFrom = static_cast<size_t>(std::max(0, fromCol + qlen));

            size_t pos = caseInsensitive
                         ? FindCaseInsensitive(ln, query, searchFrom)
                         : ln.find(query, searchFrom);
            if (pos != std::string::npos)
            {
                foundRow = r;
                foundCol = static_cast<int>(pos);
                return true;
            }
        }
    }
    return false;
}
