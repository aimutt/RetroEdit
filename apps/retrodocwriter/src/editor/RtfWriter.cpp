#include "RtfWriter.h"
#include "CharStyle.h"
#include <cstdio>
#include <fstream>

namespace
{
    // Append a single character to `out`, RTF-escaping the three reserved
    // characters and using \'hh for non-ASCII bytes so the file stays
    // pure ANSI 7-bit even when the buffer contains high-bit characters
    // (e.g. from a paste).
    void EmitChar(std::string& out, char ch)
    {
        unsigned char uc = static_cast<unsigned char>(ch);
        if (ch == '\\') { out += "\\\\"; return; }
        if (ch == '{')  { out += "\\{";  return; }
        if (ch == '}')  { out += "\\}";  return; }
        if (uc < 0x20)
        {
            // Control chars (other than \n which the caller maps to \par)
            // are dropped. \t could be supported later via \tab.
            return;
        }
        if (uc < 0x80)
        {
            out += ch;
            return;
        }
        // High-bit byte: emit as \'hh (RTF "hex escape").
        char buf[8];
        std::snprintf(buf, sizeof(buf), "\\'%02x", uc);
        out += buf;
    }

    // Emit the diff between the current and target styles. RTF treats the
    // \b / \b0 control words as toggles applied to subsequent text until
    // the next group close or another toggle. We emit just the bits that
    // changed since the last character.
    void EmitStyleDiff(std::string& out, uint8_t cur, uint8_t want)
    {
        uint8_t turnedOn  = static_cast<uint8_t>(~cur & want);
        uint8_t turnedOff = static_cast<uint8_t>(cur & ~want);
        if (turnedOn  & CharStyle::Bold)          out += "\\b";
        if (turnedOff & CharStyle::Bold)          out += "\\b0";
        if (turnedOn  & CharStyle::Italic)        out += "\\i";
        if (turnedOff & CharStyle::Italic)        out += "\\i0";
        if (turnedOn  & CharStyle::Underline)     out += "\\ul";
        if (turnedOff & CharStyle::Underline)     out += "\\ulnone";
        if (turnedOn  & CharStyle::Strikethrough) out += "\\strike";
        if (turnedOff & CharStyle::Strikethrough) out += "\\strike0";

        // A control word that ran straight into a text byte without
        // intervening delimiter is ambiguous; insert a single space which
        // RTF treats as the control-word terminator and not as content.
        uint8_t changed = static_cast<uint8_t>(turnedOn | turnedOff);
        if (changed != 0) out += ' ';
    }
}

namespace RtfWriter
{

std::string Write(const FormattedTextBuffer& buf,
                  FontFace documentFont, int pointSize)
{
    std::string out;
    out.reserve(1024);

    // Header. \fs is "half points" — point size doubled.
    out += "{\\rtf1\\ansi\\ansicpg1252\\deff0\n";
    out += "{\\fonttbl{\\f0 ";
    out += FontFaceFamily(documentFont);
    out += ";}}\n";
    char fsbuf[32];
    std::snprintf(fsbuf, sizeof(fsbuf), "\\fs%d\n", pointSize * 2);
    out += fsbuf;

    // Body.
    uint8_t curStyle = 0;
    for (int row = 0; row < buf.LineCount(); ++row)
    {
        const std::string& line = buf.Line(row);
        for (size_t c = 0; c < line.size(); ++c)
        {
            uint8_t want = buf.StyleAt(row, static_cast<int>(c));
            if (want != curStyle)
            {
                EmitStyleDiff(out, curStyle, want);
                curStyle = want;
            }
            EmitChar(out, line[c]);
        }
        // End-of-line: emit \par except after the last line, so the file
        // contains exactly LineCount-1 paragraph breaks.
        if (row + 1 < buf.LineCount())
            out += "\\par\n";
    }

    // Clean up trailing style so the closing brace doesn't leave a
    // dangling state for any reader that surfaces it.
    if (curStyle != 0)
        EmitStyleDiff(out, curStyle, 0);

    out += "}\n";
    return out;
}

bool WriteFile(const std::string& path,
               const FormattedTextBuffer& buf,
               FontFace documentFont, int pointSize)
{
    std::ofstream file(path, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!file.is_open()) return false;
    std::string body = Write(buf, documentFont, pointSize);
    file.write(body.data(), static_cast<std::streamsize>(body.size()));
    return file.good();
}

} // namespace RtfWriter
