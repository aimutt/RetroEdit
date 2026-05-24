#include "RichFileDocument.h"
#include "RtfReader.h"
#include "RtfWriter.h"
#include <cctype>
#include <fstream>
#include <vector>

RichFileDocument::RichFileDocument() = default;

bool RichFileDocument::IsRtfPath(const std::string& path)
{
    if (path.size() < 4) return false;
    const auto n = path.size();
    auto eq = [&](size_t pos, char want) {
        return std::tolower(static_cast<unsigned char>(path[pos]))
             == std::tolower(static_cast<unsigned char>(want));
    };
    return eq(n - 4, '.') && eq(n - 3, 'r') && eq(n - 2, 't') && eq(n - 1, 'f');
}

bool RichFileDocument::Load(const std::string& path)
{
    // Reset the captured header on every Load so a plain-text load doesn't
    // expose stale values from a previous .rtf load on the same document.
    m_loadedPointSize = 0;
    m_loadedFontFamily.clear();

    bool ok = IsRtfPath(path) ? LoadRtf(path) : LoadPlain(path);
    if (!ok) return false;
    m_filename = path;
    m_dirty    = false;
    return true;
}

bool RichFileDocument::LoadRtf(const std::string& path)
{
    RtfReader::Header header;
    if (!RtfReader::ReadFile(path, m_buffer, &header)) return false;
    m_loadedPointSize  = header.pointSize;
    m_loadedFontFamily = header.fontFamily;
    return true;
}

bool RichFileDocument::Save(FontFace font, int pointSize,
                            const RtfWriter::Page& page)
{
    if (m_filename.empty()) return false;
    bool ok = IsRtfPath(m_filename)
            ? SaveRtf  (m_filename, font, pointSize, page)
            : SavePlain(m_filename);
    if (!ok) return false;
    m_dirty = false;
    return true;
}

bool RichFileDocument::SaveAs(const std::string& path, FontFace font, int pointSize,
                              const RtfWriter::Page& page)
{
    std::string old = m_filename;
    m_filename = path;
    if (!Save(font, pointSize, page))
    {
        m_filename = old;
        return false;
    }
    return true;
}

bool RichFileDocument::SaveRtf(const std::string& path,
                               FontFace font, int pointSize,
                               const RtfWriter::Page& page) const
{
    return RtfWriter::WriteFile(path, m_buffer, font, pointSize, page);
}

bool RichFileDocument::LoadPlain(const std::string& path)
{
    std::ifstream file(path, std::ios::in);
    if (!file.is_open()) return false;

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(std::move(line));
    }
    m_buffer.SetLinesPlain(std::move(lines));
    return true;
}

bool RichFileDocument::SavePlain(const std::string& path) const
{
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file.is_open()) return false;
    const TextBuffer& text = m_buffer.Text();
    for (int i = 0; i < text.LineCount(); ++i)
    {
        if (i > 0) file << '\n';
        file << text.Line(i);
    }
    return true;
}

std::string RichFileDocument::DisplayName() const
{
    if (m_filename.empty())
        return "untitled";
    auto slash = m_filename.find_last_of("/\\");
    return (slash != std::string::npos) ? m_filename.substr(slash + 1) : m_filename;
}
