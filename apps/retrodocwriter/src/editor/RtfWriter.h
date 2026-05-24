#pragma once
#include "FormattedTextBuffer.h"
#include "render/FontFace.h"
#include <string>

// Serializes a FormattedTextBuffer to RTF. Writes a minimal subset:
//
//   {\rtf1\ansi\ansicpg1252\deff0
//   {\fonttbl{\f0 <fontFamily>;}}
//   \fs<pointSize*2>
//   <body with \b/\i/\ul/\strike differential control words, \par per
//    line, and \\ \{ \} escapes for literal characters>
//   }
//
// The subset is intentionally narrow — no colors, no per-run font face
// or size changes, no tables/lists/styles. Any reader that understands
// at least bold/italic/underline/strikethrough/paragraphs (WordPad,
// LibreOffice, Word, TextEdit) will render the file correctly. Unknown
// control words elsewhere in a document we generate would be skipped by
// such readers, so the format stays interoperable as we extend the
// subset in later phases.
namespace RtfWriter
{
    // Page layout (in inches) emitted into the RTF header so other readers
    // (LibreOffice, Word) compose paragraphs with the same usable width
    // as RetroDocWriter's on-screen WYSIWYG view. Default: US Letter, 1"
    // margins all around — matches WysiwygMargins's defaults.
    struct Page
    {
        double widthIn        = 8.5;
        double heightIn       = 11.0;
        double marginLeftIn   = 1.0;
        double marginRightIn  = 1.0;
        double marginTopIn    = 1.0;
        double marginBottomIn = 1.0;
    };

    // Builds the full RTF text in memory. The on-disk file should be a
    // straight byte-for-byte write of the returned string.
    std::string Write(const FormattedTextBuffer& buf,
                      FontFace documentFont, int pointSize,
                      const Page& page = {});

    // Writes Write(buf, font, size, page) to `path`. Returns false on any
    // I/O failure (open, write).
    bool WriteFile(const std::string& path,
                   const FormattedTextBuffer& buf,
                   FontFace documentFont, int pointSize,
                   const Page& page = {});
}
