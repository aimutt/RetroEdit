#pragma once
#include <string>
#include <vector>

class TextBuffer
{
public:
    TextBuffer();

    int                LineCount()                  const;
    const std::string& Line(int lineIndex)           const;
    int                LineLength(int lineIndex)     const;

    void SetLines(std::vector<std::string> lines);

    // Single-character / line edits (Stage 2)
    void InsertChar(int col, int line, char ch);
    void Backspace(int col, int line);      // erases char before col; joins with prev line if col==0
    void DeleteForward(int col, int line);  // erases char at col; joins with next line if at EOL
    void InsertNewline(int col, int line);  // splits line at col

    // Block operations (Stage 3)
    // Extract text in [startRow:startCol, endRow:endCol)  (endCol exclusive)
    std::string GetText(int startRow, int startCol, int endRow, int endCol) const;
    // Delete the same range
    void DeleteRange(int startRow, int startCol, int endRow, int endCol);
    // Insert possibly-multi-line text at (col, line); cursor end pos returned via out params
    void InsertText(int col, int line, const std::string& text,
                    int& outEndRow, int& outEndCol);

    // Forward search with wrap-around; returns false if not found
    bool FindNext(const std::string& query, int fromRow, int fromCol,
                  int& foundRow, int& foundCol,
                  bool caseInsensitive = false) const;

private:
    std::vector<std::string> m_lines;
};
