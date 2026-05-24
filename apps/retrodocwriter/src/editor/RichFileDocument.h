#pragma once
#include "FormattedTextBuffer.h"
#include "RtfWriter.h"
#include "render/FontFace.h"
#include <string>

// RetroDocWriter's document type. Mirrors the API of core/editor/FileDocument
// (filename, dirty flag, Buffer accessor, Load/Save/SaveAs/DisplayName) but
// owns a FormattedTextBuffer instead of a plain TextBuffer.
//
// Load/Save dispatch on filename extension:
//   .rtf (case-insensitive) — round-trips through RtfReader / RtfWriter,
//                             preserving formatting.
//   anything else           — plain-text I/O (one line per file line);
//                             formatting is read as all-zero, and writing
//                             discards formatting.
//
// Step 4 ships only the plain-text path. The .rtf dispatch arrives in
// Step 7 along with RtfReader / RtfWriter.
class RichFileDocument
{
public:
    RichFileDocument();

    bool Load(const std::string& path);
    // `font` and `pointSize` are baked into the RTF header when saving a
    // .rtf file. `page` is the page geometry (paper size + margins) the
    // doc was authored against — emitted into the RTF so external
    // readers (LibreOffice, Word) wrap at the same column count as
    // RetroDocWriter's on-screen view. All three are ignored when saving
    // plain text.
    bool Save(FontFace font, int pointSize, const RtfWriter::Page& page = {});
    bool SaveAs(const std::string& path, FontFace font, int pointSize,
                const RtfWriter::Page& page = {});

    const std::string& Filename()    const { return m_filename; }
    bool               IsDirty()     const { return m_dirty; }
    void               MarkDirty()         { m_dirty = true; }
    void               MarkClean()         { m_dirty = false; }

    FormattedTextBuffer&       Buffer()       { return m_buffer; }
    const FormattedTextBuffer& Buffer() const { return m_buffer; }

    // Header captured from the most recent RTF load. pointSize == 0 / empty
    // family means "the file didn't declare one" or "it wasn't an .rtf".
    // Application reads these after Load and applies them to FontSettings
    // so opening an .rtf restores the size + face it was saved at.
    int                 LoadedPointSize()  const { return m_loadedPointSize; }
    const std::string&  LoadedFontFamily() const { return m_loadedFontFamily; }

    std::string DisplayName() const;

    // True if `path` ends with .rtf (case-insensitive). Useful at call sites
    // that need to branch on file type (e.g. the Save dispatch).
    static bool IsRtfPath(const std::string& path);

private:
    bool LoadPlain(const std::string& path);
    bool LoadRtf  (const std::string& path);
    bool SavePlain(const std::string& path) const;
    bool SaveRtf  (const std::string& path, FontFace font, int pointSize,
                   const RtfWriter::Page& page) const;

    FormattedTextBuffer m_buffer;
    std::string         m_filename;
    bool                m_dirty = false;
    int                 m_loadedPointSize = 0;
    std::string         m_loadedFontFamily;
};
