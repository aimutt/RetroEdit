#pragma once
#include "CharStyle.h"
#include "editor/TextBuffer.h"
#include <cstdint>
#include <string>
#include <vector>

// Plain-text TextBuffer plus a parallel byte-per-character style vector.
//
// Composition (not inheritance): TextBuffer stays exactly as RetroEdit
// uses it. FormattedTextBuffer mirrors every mutating call into both the
// text layer and m_styles so the two stay perfectly aligned — for every
// (row, col) pair valid in the text buffer, m_styles[row][col] is a
// CharStyle bitmask describing that character's formatting.
//
// Read accessors forward to the inner TextBuffer (Line/LineLength/etc),
// so existing call sites that expected a TextBuffer-shaped interface
// keep working unchanged.
class FormattedTextBuffer
{
public:
    FormattedTextBuffer();

    // Read access ----------------------------------------------------------
    const TextBuffer& Text() const                   { return m_text; }
    int                LineCount()                  const { return m_text.LineCount(); }
    const std::string& Line(int row)                 const { return m_text.Line(row); }
    int                LineLength(int row)           const { return m_text.LineLength(row); }
    uint8_t            StyleAt(int row, int col)    const;
    bool               HasAnyFormatting()            const;

    // Bulk replacement -----------------------------------------------------
    // Replace text + style layers in one shot. Sizes must match: styles
    // must have the same shape (one inner vector per line, sized identically
    // to the corresponding string).
    void SetLines(std::vector<std::string> lines,
                  std::vector<std::vector<uint8_t>> styles);
    // Convenience: replace text and reset every character's style to 0.
    void SetLinesPlain(std::vector<std::string> lines);

    // Mutating ops mirror TextBuffer ---------------------------------------
    void InsertChar(int col, int row, char ch, uint8_t style);
    void Backspace(int col, int row);
    void DeleteForward(int col, int row);
    void InsertNewline(int col, int row);
    void InsertText(int col, int row, const std::string& text, uint8_t style,
                    int& outEndRow, int& outEndCol);
    void DeleteRange(int startRow, int startCol, int endRow, int endCol);

    // Style-only mutations --------------------------------------------------
    // Returns true if every character in [start, end) (endCol exclusive) has
    // every bit of `bits` set. An empty range returns false. Used by the
    // "smart toggle" UX: select-and-press-Bold turns the bit off when the
    // whole selection is already bold, on otherwise.
    bool AllInRangeHaveStyle(int startRow, int startCol,
                             int endRow,   int endCol,
                             uint8_t bits) const;
    // Set or clear `bits` across [start, end). `on=true` ORs the bits in;
    // `on=false` ANDs ~bits.
    void SetStyleInRange(int startRow, int startCol,
                         int endRow,   int endCol,
                         uint8_t bits, bool on);
    // Zero out every style byte (used when "flatten and save as plain text").
    void FlattenAllStyles();

private:
    // Ensure m_styles[row] has at least `len` entries, padding with zeros.
    // Used after any text-layer change to keep the two vectors aligned.
    void EnsureLineStyleSize(int row, size_t len);

    TextBuffer                              m_text;
    std::vector<std::vector<uint8_t>>       m_styles;
};
