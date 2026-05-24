#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

// Small SDL-free UTF-8 helpers used by the renderers and the editing
// operations (cursor stepping, Backspace, Delete Forward).
//
// The buffer model is byte-oriented: TextBuffer stores `std::string` lines
// containing raw UTF-8 bytes, and cursor/selection columns are byte
// indices into those lines. These helpers let the byte-indexed model
// behave correctly with multi-byte codepoints by:
//   * decoding a codepoint at a byte position (for rendering),
//   * detecting continuation bytes (so renderers can mark them zero-width),
//   * jumping over a whole codepoint (for cursor movement and deletion).
//
// All four functions are defensive: malformed UTF-8 falls back to one byte
// of progress so callers never loop forever or read past the end.

inline bool Utf8IsContinuationByte(unsigned char b)
{
    return (b & 0xC0) == 0x80;
}

// Length of the UTF-8 sequence whose lead byte is `leadByte` (1..4).
// Returns 1 for malformed lead bytes (incl. stray continuation bytes) so
// callers always make progress.
inline int Utf8CodepointSize(unsigned char leadByte)
{
    if      ((leadByte & 0x80) == 0x00) return 1;   // 0xxxxxxx
    else if ((leadByte & 0xE0) == 0xC0) return 2;   // 110xxxxx
    else if ((leadByte & 0xF0) == 0xE0) return 3;   // 1110xxxx
    else if ((leadByte & 0xF8) == 0xF0) return 4;   // 11110xxx
    return 1;                                       // malformed → step 1 byte
}

// Decodes the codepoint starting at lineBytes[pos]. Writes the byte
// offset of the next codepoint into outNext. On any malformed or
// truncated sequence, falls back to (lineBytes[pos], pos + 1) so
// renderers never stall.
inline char32_t Utf8DecodeAt(const std::string& lineBytes, size_t pos, size_t& outNext)
{
    const size_t n = lineBytes.size();
    if (pos >= n) { outNext = n; return U' '; }

    unsigned char lead = static_cast<unsigned char>(lineBytes[pos]);
    int seqLen = Utf8CodepointSize(lead);
    if (pos + static_cast<size_t>(seqLen) > n) seqLen = 1;  // truncated

    char32_t cp = 0;
    switch (seqLen)
    {
        case 1:
            cp = static_cast<char32_t>(lead);
            break;
        case 2:
        {
            unsigned char b1 = static_cast<unsigned char>(lineBytes[pos + 1]);
            if (!Utf8IsContinuationByte(b1)) { seqLen = 1; cp = lead; break; }
            cp = (static_cast<char32_t>(lead & 0x1F) << 6)
               |  static_cast<char32_t>(b1   & 0x3F);
            break;
        }
        case 3:
        {
            unsigned char b1 = static_cast<unsigned char>(lineBytes[pos + 1]);
            unsigned char b2 = static_cast<unsigned char>(lineBytes[pos + 2]);
            if (!Utf8IsContinuationByte(b1) || !Utf8IsContinuationByte(b2))
            { seqLen = 1; cp = lead; break; }
            cp = (static_cast<char32_t>(lead & 0x0F) << 12)
               | (static_cast<char32_t>(b1   & 0x3F) << 6)
               |  static_cast<char32_t>(b2   & 0x3F);
            break;
        }
        case 4:
        {
            unsigned char b1 = static_cast<unsigned char>(lineBytes[pos + 1]);
            unsigned char b2 = static_cast<unsigned char>(lineBytes[pos + 2]);
            unsigned char b3 = static_cast<unsigned char>(lineBytes[pos + 3]);
            if (!Utf8IsContinuationByte(b1) || !Utf8IsContinuationByte(b2)
                || !Utf8IsContinuationByte(b3))
            { seqLen = 1; cp = lead; break; }
            cp = (static_cast<char32_t>(lead & 0x07) << 18)
               | (static_cast<char32_t>(b1   & 0x3F) << 12)
               | (static_cast<char32_t>(b2   & 0x3F) << 6)
               |  static_cast<char32_t>(b3   & 0x3F);
            break;
        }
        default:
            cp = lead;
            break;
    }
    outNext = pos + static_cast<size_t>(seqLen);
    return cp;
}

// Returns the byte offset of the lead byte of the codepoint that
// contains position `pos`. Scans backward over continuation bytes.
// If `pos` already points at a lead byte (or 0), returns `pos`.
inline size_t Utf8LeadByteOffset(const std::string& lineBytes, size_t pos)
{
    if (pos > lineBytes.size()) pos = lineBytes.size();
    while (pos > 0
           && Utf8IsContinuationByte(static_cast<unsigned char>(lineBytes[pos])))
        --pos;
    return pos;
}
