#include "RtfWriter.h"
#include "editor/CharStyle.h"
#include "editor/Palette.h"
#include "render/FontSettings.h"
#include <cstdio>
#include <fstream>
#include <unordered_map>
#include <vector>

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
        if (uc < 0x20) return; // dropped (newline handled at line boundary)
        if (uc < 0x80) { out += ch; return; }
        char buf[8];
        std::snprintf(buf, sizeof(buf), "\\'%02x", uc);
        out += buf;
    }

    // Emit the diff between the current and target style bitmasks.
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
        // Trailing space terminates the control word so it doesn't merge
        // with the next text byte.
        uint8_t changed = static_cast<uint8_t>(turnedOn | turnedOff);
        if (changed != 0) out += ' ';
    }
}

namespace RtfWriter
{

std::string Write(const FormattedTextBuffer& buf,
                  FontFace documentFont, int pointSize,
                  const Page& page)
{
    std::string out;
    out.reserve(1024);

    // First pass: discover every FontFace value actually used in the
    // document so we can build a complete \fonttbl. Index 0 is always the
    // document default face — that's also where Inherit-sentinel chars
    // resolve to, so we don't need to emit \fN for unformatted runs.
    std::vector<FontFace> faceIndex;
    std::unordered_map<int, int> faceLookup; // FontFace cast<int> -> table idx
    auto faceIdxFor = [&](FontFace face) -> int {
        int key = static_cast<int>(face);
        auto it = faceLookup.find(key);
        if (it != faceLookup.end()) return it->second;
        int idx = static_cast<int>(faceIndex.size());
        faceIndex.push_back(face);
        faceLookup[key] = idx;
        return idx;
    };
    faceIdxFor(documentFont); // index 0

    for (int row = 0; row < buf.LineCount(); ++row)
    {
        int len = buf.LineLength(row);
        for (int c = 0; c < len; ++c)
        {
            CharFormat f = buf.FormatAt(row, c);
            if (f.face != CharFormat::Inherit
                && f.face < static_cast<uint8_t>(FontFace::Count_))
            {
                faceIdxFor(static_cast<FontFace>(f.face));
            }
        }
    }

    // Header.
    out += "{\\rtf1\\ansi\\ansicpg1252\\deff0\n";
    out += "{\\fonttbl";
    for (size_t i = 0; i < faceIndex.size(); ++i)
    {
        char hdr[32];
        std::snprintf(hdr, sizeof(hdr), "{\\f%d ", static_cast<int>(i));
        out += hdr;
        out += FontFaceFamily(faceIndex[i]);
        out += ";}";
    }
    out += "}\n";
    // Color table — always emit the full 16-color palette in fixed order
    // so reader code can map palette index ↔ \cfN with no per-document
    // table negotiation. RTF convention: a leading empty entry (just `;`)
    // is "auto"; \cf0 in our subset means "no override" (Inherit).
    out += "{\\colortbl;";
    for (int i = 0; i < Palette::kCount; ++i)
    {
        Color cc = Palette::ColorAt(static_cast<uint8_t>(i));
        char rgb[40];
        std::snprintf(rgb, sizeof(rgb), "\\red%d\\green%d\\blue%d;", cc.r, cc.g, cc.b);
        out += rgb;
    }
    out += "}\n";

    // Page geometry — emit in twips (1 inch = 1440 twips) so other readers
    // (LibreOffice, Word) compose paragraphs at the same usable width as
    // our on-screen WYSIWYG view. Without these, LibreOffice falls back
    // to its own page-size + margin defaults and wraps lines at a
    // different column than what we drew.
    auto toTwips = [](double inches) {
        return static_cast<int>(inches * 1440.0 + 0.5);
    };
    char pageBuf[160];
    std::snprintf(pageBuf, sizeof(pageBuf),
        "\\paperw%d\\paperh%d\\margl%d\\margr%d\\margt%d\\margb%d\n",
        toTwips(page.widthIn),        toTwips(page.heightIn),
        toTwips(page.marginLeftIn),   toTwips(page.marginRightIn),
        toTwips(page.marginTopIn),    toTwips(page.marginBottomIn));
    out += pageBuf;

    char fsbuf[32];
    std::snprintf(fsbuf, sizeof(fsbuf), "\\fs%d\n", pointSize * 2);
    out += fsbuf;

    // Body: emit differential \fN, \fsN, \cfN, and style control words.
    // \cf0 = "auto" (Inherit); palette index N → \cf<N+1> because index 0
    // in our \colortbl is the empty "auto" entry.
    uint8_t curStyle = 0;
    int     curFaceIdx = 0;          // index into faceIndex
    int     curHalfPt  = pointSize * 2;
    int     curColorRtf = 0;         // 0 = auto, 1..16 = palette index + 1
    int     curHighlightRtf = 0;     // 0 = auto/no-highlight, 1..16 = palette + 1
    for (int row = 0; row < buf.LineCount(); ++row)
    {
        const std::string& line = buf.Line(row);
        for (size_t c = 0; c < line.size(); ++c)
        {
            CharFormat f = buf.FormatAt(row, static_cast<int>(c));

            FontFace wantFace = (f.face == CharFormat::Inherit
                                 || f.face >= static_cast<uint8_t>(FontFace::Count_))
                              ? documentFont
                              : static_cast<FontFace>(f.face);
            int wantFaceIdx = faceLookup[static_cast<int>(wantFace)];
            int wantHalfPt  = (f.size == CharFormat::Inherit || f.size >= 4)
                              ? pointSize * 2
                              : FontSizePoints(FontSizeAt(static_cast<int>(f.size))) * 2;
            int wantColorRtf = (f.color == CharFormat::Inherit
                                || f.color >= Palette::kCount)
                              ? 0
                              : static_cast<int>(f.color) + 1;
            int wantHighlightRtf = (f.highlight == CharFormat::Inherit
                                    || f.highlight >= Palette::kCount)
                                  ? 0
                                  : static_cast<int>(f.highlight) + 1;

            if (wantFaceIdx != curFaceIdx)
            {
                char buf2[32];
                std::snprintf(buf2, sizeof(buf2), "\\f%d ", wantFaceIdx);
                out += buf2;
                curFaceIdx = wantFaceIdx;
            }
            if (wantHalfPt != curHalfPt)
            {
                char buf2[32];
                std::snprintf(buf2, sizeof(buf2), "\\fs%d ", wantHalfPt);
                out += buf2;
                curHalfPt = wantHalfPt;
            }
            if (wantColorRtf != curColorRtf)
            {
                char buf2[32];
                std::snprintf(buf2, sizeof(buf2), "\\cf%d ", wantColorRtf);
                out += buf2;
                curColorRtf = wantColorRtf;
            }
            if (wantHighlightRtf != curHighlightRtf)
            {
                char buf2[32];
                std::snprintf(buf2, sizeof(buf2), "\\highlight%d ", wantHighlightRtf);
                out += buf2;
                curHighlightRtf = wantHighlightRtf;
            }
            if (f.style != curStyle)
            {
                EmitStyleDiff(out, curStyle, f.style);
                curStyle = f.style;
            }
            EmitChar(out, line[c]);
        }
        if (row + 1 < buf.LineCount())
        {
            out += "\\par\n";
            // Forced page break before the next row → emit \page right
            // after the \par so RTF readers (Word/WordPad/LibreOffice)
            // start the following paragraph on a new page.
            if (buf.PageBreakBefore(row + 1))
                out += "\\page\n";
        }
    }
    // Note: a page break flagged on row 0 is effectively no-op (the doc
    // already starts on its first page). We don't emit a leading \page.

    // Clean up trailing style so the closing brace doesn't leave a
    // dangling state for any reader that surfaces it.
    if (curStyle != 0)
        EmitStyleDiff(out, curStyle, 0);

    out += "}\n";
    return out;
}

bool WriteFile(const std::string& path,
               const FormattedTextBuffer& buf,
               FontFace documentFont, int pointSize,
               const Page& page)
{
    std::ofstream file(path, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!file.is_open()) return false;
    std::string body = Write(buf, documentFont, pointSize, page);
    file.write(body.data(), static_cast<std::streamsize>(body.size()));
    return file.good();
}

} // namespace RtfWriter
