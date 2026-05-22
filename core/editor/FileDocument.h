#pragma once
#include "TextBuffer.h"
#include <string>

class FileDocument
{
public:
    FileDocument();

    bool Load(const std::string& path);
    bool Save();
    bool SaveAs(const std::string& path);

    const std::string& Filename()    const { return m_filename; }
    bool               IsDirty()     const { return m_dirty; }
    void               MarkDirty()         { m_dirty = true; }
    void               MarkClean()         { m_dirty = false; }

    TextBuffer&       Buffer()       { return m_buffer; }
    const TextBuffer& Buffer() const { return m_buffer; }

    // Returns bare filename (no path), or "untitled" if no file is open
    std::string DisplayName() const;

private:
    TextBuffer  m_buffer;
    std::string m_filename;
    bool        m_dirty = false;
};
