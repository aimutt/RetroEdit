#pragma once
#include "FormattedTextBuffer.h"
#include <string>

// Parses an RTF byte stream into a FormattedTextBuffer.
//
// Recognized control words (everything else is consumed and skipped):
//   \b  / \b0          bold on/off
//   \i  / \i0          italic on/off
//   \ul / \ulnone      underline on/off (also accepts \ul0)
//   \strike / \strike0 strikethrough on/off
//   \par               paragraph break (= newline)
//   \line              soft line break (treated as newline)
//   \\                 literal backslash
//   \{ / \}            literal brace
//   \'hh               hex byte escape
//
// Destination groups recognized and entirely skipped (the file may still
// emit control words inside them, e.g. \fonttbl's font names):
//   \fonttbl, \stylesheet, \colortbl, \info, \header, \footer, \pict,
//   \*<anything>       (the \* introducer means "ignorable destination")
//
// All other unknown control words are dropped, but their numeric
// parameter (if any) is consumed. The reader is **permissive** — it
// accepts anything WordPad / LibreOffice / Word write within our scope
// and silently drops what it doesn't understand. Returns false only on
// I/O / empty-input errors; malformed RTF still produces some output.
namespace RtfReader
{
    // Header metadata captured during parse. Both fields are optional —
    // the parse succeeds even when the file omits them.
    struct Header
    {
        // Document-level point size from the top-level \fs<N> control word
        // (N is half-points; e.g. \fs24 → 12 points). 0 = not specified.
        int pointSize = 0;
        // First entry of the \fonttbl group (the body font name). Empty
        // when not specified or unparseable.
        std::string fontFamily;
    };

    bool Read(const std::string& rtf, FormattedTextBuffer& out, Header* outHeader = nullptr);
    bool ReadFile(const std::string& path, FormattedTextBuffer& out, Header* outHeader = nullptr);
}
