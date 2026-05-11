#include "FileDocument.h"
#include <fstream>
#include <vector>

FileDocument::FileDocument() = default;

bool FileDocument::Load(const std::string& path)
{
    std::ifstream file(path, std::ios::in);
    if (!file.is_open())
        return false;

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(std::move(line));
    }

    m_buffer.SetLines(std::move(lines));
    m_filename = path;
    m_dirty    = false;
    return true;
}

bool FileDocument::Save()
{
    if (m_filename.empty())
        return false;

    std::ofstream file(m_filename, std::ios::out | std::ios::trunc);
    if (!file.is_open())
        return false;

    for (int i = 0; i < m_buffer.LineCount(); ++i)
    {
        if (i > 0) file << '\n';
        file << m_buffer.Line(i);
    }

    m_dirty = false;
    return true;
}

bool FileDocument::SaveAs(const std::string& path)
{
    std::string old = m_filename;
    m_filename = path;
    if (!Save())
    {
        m_filename = old;
        return false;
    }
    return true;
}

std::string FileDocument::DisplayName() const
{
    if (m_filename.empty())
        return "untitled";

    auto slash = m_filename.find_last_of("/\\");
    return (slash != std::string::npos) ? m_filename.substr(slash + 1) : m_filename;
}
