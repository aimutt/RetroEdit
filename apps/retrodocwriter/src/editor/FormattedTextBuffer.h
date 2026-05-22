#pragma once
#include "editor/CharStyle.h"
#include "editor/TextBuffer.h"
#include <cstdint>
#include <string>
#include <vector>

// Plain-text TextBuffer plus a parallel per-character CharFormat vector.
//
// Composition (not inheritance): TextBuffer stays exactly as RetroEdit
// uses it. FormattedTextBuffer mirrors every mutating call into both the
// text layer and m_formats so the two stay perfectly aligned — for every
// (row, col) pair valid in the text buffer, m_formats[row][col] is a
// CharFormat record describing that character's style/face/size.
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
    CharFormat         FormatAt(int row, int col)   const;
    // Direct access to the full per-line format vectors. Used by the
    // print path (which iterates per-character to switch GDI fonts) and
    // by the RTF writer's pre-pass that enumerates faces in the document.
    const std::vector<std::vector<CharFormat>>& Formats() const { return m_formats; }
    uint8_t            StyleAt(int row, int col)    const { return FormatAt(row, col).style; }
    uint8_t            FaceAt (int row, int col)    const { return FormatAt(row, col).face;  }
    uint8_t            SizeAt (int row, int col)    const { return FormatAt(row, col).size;  }
    // True if any character carries a non-default style/face/size.
    bool               HasAnyFormatting()            const;

    // Bulk replacement -----------------------------------------------------
    // Replace text + format layers in one shot. Sizes must match: formats
    // must have the same shape (one inner vector per line, sized identically
    // to the corresponding string).
    void SetLines(std::vector<std::string> lines,
                  std::vector<std::vector<CharFormat>> formats);
    // Convenience: replace text and reset every character's format to
    // default (style=0, face=Inherit, size=Inherit).
    void SetLinesPlain(std::vector<std::string> lines);

    // Mutating ops mirror TextBuffer ---------------------------------------
    // Newly inserted characters use the given CharFormat. For typed input
    // the caller passes {m_currentStyle, Inherit, Inherit}; for paste of
    // unstyled text, all three default to Inherit/0.
    void InsertChar(int col, int row, char ch, CharFormat fmt);
    void Backspace(int col, int row);
    void DeleteForward(int col, int row);
    void InsertNewline(int col, int row);
    void InsertText(int col, int row, const std::string& text, CharFormat fmt,
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
    // Set or clear style `bits` across [start, end). `on=true` ORs the bits
    // in; `on=false` ANDs ~bits. Face/size on those characters are not
    // touched.
    void SetStyleInRange(int startRow, int startCol,
                         int endRow,   int endCol,
                         uint8_t bits, bool on);
    // Pin face/size across [start, end). Pass CharFormat::Inherit to clear
    // a per-run override and let the document default take over again.
    void SetFaceInRange (int startRow, int startCol,
                         int endRow,   int endCol,
                         uint8_t faceIdx);
    void SetSizeInRange (int startRow, int startCol,
                         int endRow,   int endCol,
                         uint8_t sizeIdx);
    // Zero out style and reset face/size to Inherit across the entire
    // buffer (used when "flatten and save as plain text").
    void FlattenAllStyles();

private:
    // Ensure m_formats[row] has at least `len` entries, padding with the
    // default CharFormat (= no overrides). Used after any text-layer
    // change to keep the two vectors aligned.
    void EnsureLineFormatSize(int row, size_t len);

    TextBuffer                              m_text;
    std::vector<std::vector<CharFormat>>    m_formats;
};
