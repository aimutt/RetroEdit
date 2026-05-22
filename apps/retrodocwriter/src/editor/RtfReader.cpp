#include "RtfReader.h"
#include "editor/CharStyle.h"
#include "render/FontFace.h"
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace
{
    // Map a body-encountered point size to the closest FontSize enum index.
    // FontSize has four discrete values (Small=12, Medium=16, Large=20,
    // ExtraLarge=24); anything in between snaps to the nearest bucket.
    uint8_t MapPointsToSizeIdx(int pt)
    {
        if (pt <= 13) return 0; // Small
        if (pt <= 17) return 1; // Medium
        if (pt <= 22) return 2; // Large
        return 3;               // ExtraLarge
    }

    // Single-pass RTF parser. We don't build an AST — we walk the byte
    // stream and emit characters with the current style/face/size as we go.
    //
    // State that matters:
    //   - braceDepth, skipFromDepth, styleStack, curStyle: as before
    //   - fontTblOpenDepth: brace depth where \fonttbl opened (0 = not in)
    //   - fontTblNames: map of \fN index -> family name parsed from
    //     {\f0 Cascadia Mono;}{\f1 JetBrains Mono;}... groups
    //   - curFaceIdx: body-level \fN state. -1 means "no \fN seen since
    //     start of body" (= Inherit). \fN sets it to N; the resolved face
    //     comes from fontTblNames + FontFaceFromFamilyName.
    //   - curHalfPt:   body-level \fsN state. 0 = "no body \fs seen" (=
    //     Inherit). headerFsHalfPoints captures the pre-body \fs so the
    //     resolver can fold "matches doc default" back to Inherit.
    struct Parser
    {
        const std::string& src;
        FormattedTextBuffer& out;
        size_t pos = 0;
        int braceDepth   = 0;
        int skipFromDepth = 0;
        uint8_t curStyle = 0;
        std::vector<uint8_t> styleStack;
        std::vector<std::string>            lines;
        std::vector<std::vector<CharFormat>> formatRows;

        // Header capture
        int headerFsHalfPoints = 0;
        int defaultFontIdx     = 0;     // \deffN value (defaults to 0)

        // Font table parsing
        int fontTblOpenDepth = 0;
        int captureFontIdx   = -1;      // -1 = not capturing a name
        std::string captureFontName;
        std::unordered_map<int, std::string> fontTblNames;

        // Body run state
        int curFaceIdx = -1;            // -1 = Inherit
        int curHalfPt  = 0;             // 0  = Inherit

        Parser(const std::string& s, FormattedTextBuffer& o) : src(s), out(o)
        {
            lines.emplace_back();
            formatRows.emplace_back();
        }

        bool skipping() const { return skipFromDepth > 0; }

        // Resolve curFaceIdx → CharFormat::face byte. Inherit when
        //   - no body \fN seen yet
        //   - curFaceIdx matches the document-default index (\deffN, ~ 0)
        //     so chars at the doc default round-trip without becoming
        //     "locked-in" explicit overrides.
        uint8_t ResolveFaceForEmit() const
        {
            if (curFaceIdx < 0) return CharFormat::Inherit;
            if (curFaceIdx == defaultFontIdx) return CharFormat::Inherit;
            auto it = fontTblNames.find(curFaceIdx);
            if (it == fontTblNames.end()) return CharFormat::Inherit;
            FontFace face;
            if (!FontFaceFromFamilyName(it->second.c_str(), face))
                return CharFormat::Inherit;
            return static_cast<uint8_t>(face);
        }

        // Resolve curHalfPt → CharFormat::size byte. Inherit when
        //   - no body \fsN seen yet (== 0)
        //   - body \fsN matches the header \fs (the doc default)
        uint8_t ResolveSizeForEmit() const
        {
            if (curHalfPt <= 0) return CharFormat::Inherit;
            if (headerFsHalfPoints > 0 && curHalfPt == headerFsHalfPoints)
                return CharFormat::Inherit;
            return MapPointsToSizeIdx(curHalfPt / 2);
        }

        void emitByte(unsigned char b)
        {
            if (skipping()) return;
            if (b == '\n')
            {
                lines.emplace_back();
                formatRows.emplace_back();
            }
            else if (b == '\r' || b == 0)
            {
                // RFC: ignore \r in RTF body; \0 is invalid.
            }
            else
            {
                lines.back().push_back(static_cast<char>(b));
                CharFormat f;
                f.style = curStyle;
                f.face  = ResolveFaceForEmit();
                f.size  = ResolveSizeForEmit();
                formatRows.back().push_back(f);
            }
        }

        static int HexDigit(char c)
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        bool ReadOptionalParam(int& outVal)
        {
            size_t start = pos;
            bool neg = false;
            if (pos < src.size() && src[pos] == '-') { neg = true; ++pos; }
            if (pos >= src.size() || !std::isdigit(static_cast<unsigned char>(src[pos])))
            {
                pos = start;
                return false;
            }
            int v = 0;
            while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos])))
            {
                v = v * 10 + (src[pos] - '0');
                ++pos;
            }
            outVal = neg ? -v : v;
            return true;
        }

        void ConsumeControlWordDelimiter()
        {
            if (pos < src.size() && src[pos] == ' ')
                ++pos;
        }

        bool IsDestinationKeyword(const std::string& w) const
        {
            return w == "fonttbl" || w == "filetbl" || w == "colortbl"
                || w == "stylesheet" || w == "listtable" || w == "rsidtbl"
                || w == "info" || w == "pict" || w == "header" || w == "footer"
                || w == "headerl" || w == "headerr" || w == "footerl" || w == "footerr"
                || w == "themedata" || w == "datastore" || w == "object"
                || w == "field" || w == "shppict" || w == "nonshppict"
                || w == "bkmkstart" || w == "bkmkend" || w == "xe" || w == "tc";
        }

        // True iff we haven't emitted any body text yet (so \fs / \deff
        // seen now refers to document-level defaults).
        bool InHeader() const
        {
            return lines.size() == 1 && lines.back().empty();
        }

        void HandleControlWord(const std::string& word, bool hasParam, int param)
        {
            if (word == "par" || word == "line")
            {
                emitByte('\n');
                return;
            }
            auto setBit = [&](uint8_t bit, bool on) {
                if (skipping()) return;
                if (on) curStyle |=  bit;
                else    curStyle &= ~bit;
            };
            if (word == "b")      { setBit(CharStyle::Bold,          !(hasParam && param == 0)); return; }
            if (word == "i")      { setBit(CharStyle::Italic,        !(hasParam && param == 0)); return; }
            if (word == "strike") { setBit(CharStyle::Strikethrough, !(hasParam && param == 0)); return; }
            if (word == "ul")     { setBit(CharStyle::Underline,     !(hasParam && param == 0)); return; }
            if (word == "ulnone") { setBit(CharStyle::Underline, false); return; }

            if (word == "deff" && hasParam)
            {
                defaultFontIdx = param;
                return;
            }

            if (word == "fs" && hasParam)
            {
                if (InHeader())
                    headerFsHalfPoints = param;
                else
                    curHalfPt = param;
                return;
            }

            if (word == "f" && hasParam)
            {
                // Inside \fonttbl: this introduces a font-table entry; we
                // begin capturing the literal text bytes that follow as the
                // entry's family name until ';' or '}'.
                if (fontTblOpenDepth > 0)
                {
                    captureFontIdx = param;
                    captureFontName.clear();
                    return;
                }
                // In the body: \fN switches the active face for following
                // text. The renderer falls back to doc default when the
                // index is unmapped (e.g., font missing from the table).
                curFaceIdx = param;
                return;
            }

            if (IsDestinationKeyword(word))
            {
                if (skipFromDepth == 0)
                    skipFromDepth = braceDepth;
                if (word == "fonttbl")
                    fontTblOpenDepth = braceDepth;
                return;
            }
        }

        // While inside \fonttbl and capturing a font name, drain literal
        // text bytes into captureFontName until we hit ';' or any
        // structural character; finalize on terminator.
        bool FeedFontTableCapture()
        {
            if (captureFontIdx < 0) return false;
            char peek = src[pos];
            if (peek == ';' || peek == '}' || peek == '{' || peek == '\\')
            {
                // Strip trailing whitespace from the captured name.
                while (!captureFontName.empty()
                       && (captureFontName.back() == ' '
                           || captureFontName.back() == '\t'))
                    captureFontName.pop_back();
                fontTblNames[captureFontIdx] = std::move(captureFontName);
                captureFontIdx = -1;
                captureFontName.clear();
                return false; // let normal parsing handle the char
            }
            ++pos;
            // Skip leading whitespace.
            if (!(captureFontName.empty() && (peek == ' ' || peek == '\t')))
                captureFontName.push_back(peek);
            return true;
        }

        void ParseOne()
        {
            if (FeedFontTableCapture()) return;

            char ch = src[pos++];
            if (ch == '{')
            {
                ++braceDepth;
                styleStack.push_back(curStyle);
                return;
            }
            if (ch == '}')
            {
                if (skipFromDepth > 0 && braceDepth == skipFromDepth)
                    skipFromDepth = 0;
                if (fontTblOpenDepth > 0 && braceDepth == fontTblOpenDepth)
                    fontTblOpenDepth = 0;
                if (!styleStack.empty())
                {
                    curStyle = styleStack.back();
                    styleStack.pop_back();
                }
                if (braceDepth > 0) --braceDepth;
                return;
            }
            if (ch == '\\')
            {
                if (pos >= src.size()) return;
                char ch2 = src[pos];
                if (!std::isalpha(static_cast<unsigned char>(ch2)))
                {
                    ++pos;
                    if (ch2 == '\\') { emitByte('\\'); return; }
                    if (ch2 == '{')  { emitByte('{');  return; }
                    if (ch2 == '}')  { emitByte('}');  return; }
                    if (ch2 == '\n' || ch2 == '\r')
                    {
                        emitByte('\n');
                        return;
                    }
                    if (ch2 == '\'')
                    {
                        if (pos + 1 < src.size())
                        {
                            int hi = HexDigit(src[pos]);
                            int lo = HexDigit(src[pos + 1]);
                            pos += 2;
                            if (hi >= 0 && lo >= 0)
                                emitByte(static_cast<unsigned char>((hi << 4) | lo));
                        }
                        return;
                    }
                    if (ch2 == '*')
                    {
                        if (skipFromDepth == 0)
                            skipFromDepth = braceDepth;
                        return;
                    }
                    return;
                }
                std::string word;
                while (pos < src.size() && std::isalpha(static_cast<unsigned char>(src[pos])))
                {
                    word.push_back(src[pos]);
                    ++pos;
                }
                int param = 0;
                bool hasParam = ReadOptionalParam(param);
                ConsumeControlWordDelimiter();
                HandleControlWord(word, hasParam, param);
                return;
            }
            if (ch == '\n' || ch == '\r')
            {
                return;
            }
            emitByte(static_cast<unsigned char>(ch));
        }

        void Run()
        {
            while (pos < src.size())
                ParseOne();
        }

        void Finalize()
        {
            if (lines.size() > 1 && lines.back().empty() && formatRows.back().empty())
            {
                lines.pop_back();
                formatRows.pop_back();
            }
            out.SetLines(std::move(lines), std::move(formatRows));
        }
    };
}

namespace RtfReader
{

bool Read(const std::string& rtf, FormattedTextBuffer& out, Header* outHeader)
{
    if (rtf.empty()) return false;
    Parser p(rtf, out);
    p.Run();
    p.Finalize();
    if (outHeader)
    {
        outHeader->pointSize = p.headerFsHalfPoints > 0 ? p.headerFsHalfPoints / 2 : 0;
        // Family name of the default \fN entry (index 0 unless \deff
        // overrode it). Used by Application::OpenFile to restore the
        // document's saved font face.
        auto it = p.fontTblNames.find(p.defaultFontIdx);
        if (it != p.fontTblNames.end()) outHeader->fontFamily = it->second;
        else                            outHeader->fontFamily.clear();
    }
    return true;
}

bool ReadFile(const std::string& path, FormattedTextBuffer& out, Header* outHeader)
{
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) return false;
    std::stringstream ss;
    ss << file.rdbuf();
    return Read(ss.str(), out, outHeader);
}

} // namespace RtfReader
