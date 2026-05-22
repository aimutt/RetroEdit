#include "FileSettings.h"
#include <fstream>

namespace
{
    std::string Trim(const std::string& s)
    {
        const char* ws = " \t\r\n";
        size_t a = s.find_first_not_of(ws);
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(ws);
        return s.substr(a, b - a + 1);
    }
}

bool FileSettings::Load(const std::string& path)
{
    m_values.clear();
    std::ifstream in(path, std::ios::in);
    if (!in.is_open()) return false;

    std::string line;
    while (std::getline(in, line))
    {
        std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        auto eq = trimmed.find('=');
        if (eq == std::string::npos) continue;

        std::string key = Trim(trimmed.substr(0, eq));
        std::string val = Trim(trimmed.substr(eq + 1));
        if (key.empty()) continue;
        m_values[key] = val;
    }
    return true;
}

bool FileSettings::Save(const std::string& path) const
{
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) return false;

    out << "# RetroEdit per-file settings - auto-generated.\n";
    for (const auto& kv : m_values)
        out << kv.first << '=' << kv.second << '\n';
    return out.good();
}

bool FileSettings::Has(const std::string& key) const
{
    return m_values.find(key) != m_values.end();
}

void FileSettings::SetString(const std::string& key, const std::string& value)
{
    m_values[key] = value;
}

void FileSettings::SetBool(const std::string& key, bool value)
{
    m_values[key] = value ? "true" : "false";
}

void FileSettings::SetInt(const std::string& key, int value)
{
    m_values[key] = std::to_string(value);
}

std::string FileSettings::GetString(const std::string& key, const std::string& def) const
{
    auto it = m_values.find(key);
    return (it == m_values.end()) ? def : it->second;
}

bool FileSettings::GetBool(const std::string& key, bool def) const
{
    auto it = m_values.find(key);
    if (it == m_values.end()) return def;
    const std::string& v = it->second;
    if (v == "true"  || v == "1" || v == "yes" || v == "on")  return true;
    if (v == "false" || v == "0" || v == "no"  || v == "off") return false;
    return def;
}

int FileSettings::GetInt(const std::string& key, int def) const
{
    auto it = m_values.find(key);
    if (it == m_values.end()) return def;
    try { return std::stoi(it->second); }
    catch (...) { return def; }
}

std::string FileSettings::SidecarPath(const std::string& documentPath)
{
    if (documentPath.empty()) return "";
    return documentPath + ".retroedit";
}
