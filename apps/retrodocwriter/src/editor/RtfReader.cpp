#include "RtfReader.h"
#include "CharStyle.h"
#include <cctype>
#include <fstream>
#include <sstream>
#include <vector>

namespace
{
    // Single-pass RTF parser. We don't build an AST — we walk the byte
    // stream and emit characters with the current style as we go.
    //
    // State that matters:
    //   - braceDepth: current brace nesting (starts at 0; '{' increments,
    //     '}' decrements).
    //   - skipFromDepth: when > 0, every literal-text byte and every
    //     non-style-toggling control word inside the group at depth
    //     `skipFromDepth` (and any nested) is dropped. Reset to 0 when
    //     the closing brace at that depth is seen. Style toggles
    //     encountered inside are also ignored (consistent with how
    //     destinations like \fonttbl don't actually want their
    //     content to alter the body's style).
    //   - styleStack: each '{' pushes the current style; '}' pops back.
    //     This is how RTF scopes style changes — toggles set inside a
    //     group are reverted when the group closes.
    //   - curStyle: the active style bits (initially 0).
    struct Parser
    {
        const std::string& src;
        FormattedTextBuffer& out;
        size_t pos = 0;
        int braceDepth   = 0;
        int skipFromDepth = 0;
        uint8_t curStyle = 0;
        std::vector<uint8_t> styleStack;     // saved on each '{'
        std::vector<std::string>            lines;
        std::vector<std::vector<uint8_t>>   styleRows;
        // Header capture --------------------------------------------------
        // \fs<N> half-points encountered before the first body byte.
        int     headerFsHalfPoints = 0;
        // The brace depth at which the \fonttbl group opened, or 0 when
        // not currently inside one.
        int     fontTblOpenDepth = 0;
        // Once we see \f0 inside \fonttbl, switch to capturing literal
        // text bytes into f0Family until ';' or '}' terminates the entry.
        bool    capturingF0Name = false;
        std::string f0Family;
        // True while we're between the file header and the first text
        // byte (i.e. \rtf1\ansi\fs24 etc.). We just keep this simple by
        // not treating it specially — all those control words land in
        // the unknown-bucket and are dropped without emission.

        Parser(const std::string& s, FormattedTextBuffer& o) : src(s), out(o)
        {
            lines.emplace_back();
            styleRows.emplace_back();
        }

        bool skipping() const { return skipFromDepth > 0; }

        void emitByte(unsigned char b)
        {
            if (skipping()) return;
            if (b == '\n')
            {
                lines.emplace_back();
                styleRows.emplace_back();
            }
            else if (b == '\r' || b == 0)
            {
                // RFC: ignore \r in RTF body; \0 is invalid.
            }
            else
            {
                lines.back().push_back(static_cast<char>(b));
                styleRows.back().push_back(curStyle);
            }
        }

        // Read a hex digit at pos (0-15) or -1 if not a hex digit.
        static int HexDigit(char c)
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        // Parse the optional signed integer parameter of a control word.
        // Returns true if a parameter was found and `outVal` is populated.
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

        // Consume the single space that RTF uses as a delimiter after a
        // control word. RTF spec: a space immediately following a control
        // word is consumed and NOT part of the body.
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

        // Handle a parsed control word (and its optional numeric param).
        // If we're inside a skip group, style toggles are no-ops; only
        // destination markers can be observed (to skip a freshly opened
        // nested group). The space delimiter is already consumed by the
        // caller.
        void HandleControlWord(const std::string& word, bool hasParam, int param)
        {
            if (word == "par" || word == "line")
            {
                emitByte('\n');
                return;
            }
            // Style toggles. RTF semantics: \b (no param) = on, \b0 = off,
            // \b1 = on. Same for \i and \strike. \ul is "on", \ulnone is
            // "off"; \ul0 is also "off" (Word writes this).
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

            // Document-level point size (half-points). RTF writers emit
            // \fsN once near the top of the file. We capture the latest
            // value seen before the first body byte; values seen later
            // (mid-document size changes) are ignored — per-run size is
            // not supported in Phase 2.
            if (word == "fs" && hasParam)
            {
                if (lines.size() == 1 && lines.back().empty())
                    headerFsHalfPoints = param;
                return;
            }

            // Font index used by following text. Inside \fonttbl, \f0
            // introduces the body-font name entry; capture its literal
            // text up to the next ';' or '}'. We only care about \f0 —
            // additional font-table entries are skipped.
            if (word == "f" && hasParam && fontTblOpenDepth > 0)
            {
                if (param == 0 && f0Family.empty())
                    capturingF0Name = true;
                return;
            }

            // Destination keyword? Start skipping the current group.
            if (IsDestinationKeyword(word))
            {
                if (skipFromDepth == 0)
                    skipFromDepth = braceDepth;   // skip until this brace closes
                if (word == "fonttbl")
                    fontTblOpenDepth = braceDepth;
                return;
            }
            // Other control words: unknown to us. Already consumed.
        }

        void ParseOne()
        {
            // While inside the \fonttbl group capturing the body font's
            // name, every literal text byte feeds f0Family until the entry
            // terminator (';') or a structural character is seen.
            if (capturingF0Name)
            {
                char peek = src[pos];
                if (peek == ';' || peek == '}' || peek == '{' || peek == '\\')
                {
                    capturingF0Name = false;
                    // fall through and let normal parsing handle the char
                }
                else
                {
                    ++pos;
                    if (!(f0Family.empty() && (peek == ' ' || peek == '\t')))
                        f0Family.push_back(peek);
                    return;
                }
            }

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
                // Control symbol: single non-alpha character following \.
                if (!std::isalpha(static_cast<unsigned char>(ch2)))
                {
                    ++pos;
                    if (ch2 == '\\') { emitByte('\\'); return; }
                    if (ch2 == '{')  { emitByte('{');  return; }
                    if (ch2 == '}')  { emitByte('}');  return; }
                    if (ch2 == '\n' || ch2 == '\r')
                    {
                        // RTF treats \<newline> as a paragraph break.
                        emitByte('\n');
                        return;
                    }
                    if (ch2 == '\'')
                    {
                        // Hex byte escape \'hh
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
                        // Ignorable-destination marker. The control word
                        // that immediately follows introduces a group we
                        // should skip even if we know the keyword.
                        if (skipFromDepth == 0)
                            skipFromDepth = braceDepth;
                        return;
                    }
                    // Unknown control symbol; drop.
                    return;
                }

                // Control word: alpha+ optionally followed by signed digits.
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
                // Whitespace between RTF tokens; ignored in the body.
                return;
            }
            // Plain text byte.
            emitByte(static_cast<unsigned char>(ch));
        }

        void Run()
        {
            while (pos < src.size())
                ParseOne();
        }

        void Finalize()
        {
            // If the parse pushed a trailing empty line because the file
            // ended with \par, drop it so the document doesn't gain a
            // phantom row vs. what was saved.
            if (lines.size() > 1 && lines.back().empty() && styleRows.back().empty())
            {
                lines.pop_back();
                styleRows.pop_back();
            }
            out.SetLines(std::move(lines), std::move(styleRows));
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
        outHeader->fontFamily = p.f0Family;
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
