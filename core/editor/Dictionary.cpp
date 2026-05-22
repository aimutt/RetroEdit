#include "Dictionary.h"
#include "Dictionary.gen.h"

#include <cctype>
#include <fstream>

namespace
{
    std::string Lower(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        for (char c : s)
            out.push_back(static_cast<char>(
                std::tolower(static_cast<unsigned char>(c))));
        return out;
    }

    std::string Trim(const std::string& s)
    {
        const char* ws = " \t\r\n";
        size_t a = s.find_first_not_of(ws);
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(ws);
        return s.substr(a, b - a + 1);
    }
}

Dictionary::Dictionary()
{
    m_builtin.reserve(static_cast<size_t>(retroedit::kDictionaryWordCount));
    for (int i = 0; i < retroedit::kDictionaryWordCount; ++i)
        m_builtin.emplace(retroedit::kDictionaryWords[i]);
}

bool Dictionary::Contains(const std::string& word) const
{
    if (word.empty()) return false;
    std::string w = Lower(word);
    if (m_userRemoved.count(w)) return false;
    return m_userAdded.count(w) != 0 || m_builtin.count(w) != 0;
}

bool Dictionary::AddWord(const std::string& word)
{
    if (word.empty()) return false;
    std::string w = Lower(word);

    // If the user previously removed this built-in word, "Add" undoes that.
    if (m_userRemoved.erase(w) > 0)
        return true;

    if (m_builtin.count(w) || m_userAdded.count(w))
        return false;

    m_userAdded.insert(std::move(w));
    return true;
}

bool Dictionary::RemoveWord(const std::string& word)
{
    if (word.empty()) return false;
    std::string w = Lower(word);

    if (m_userAdded.erase(w) > 0)
        return true;

    if (m_builtin.count(w))
    {
        if (m_userRemoved.insert(w).second)
            return true;
        return false; // already marked removed
    }

    return false; // wasn't in the dictionary to begin with
}

bool Dictionary::LoadUserOverlay(const std::string& path)
{
    m_userAdded.clear();
    m_userRemoved.clear();

    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string line;
    while (std::getline(in, line))
    {
        std::string t = Trim(line);
        if (t.empty() || t[0] == '#') continue;
        if (t.size() < 2) continue;
        char op = t[0];
        std::string w = Lower(Trim(t.substr(1)));
        if (w.empty()) continue;
        if (op == '+')      m_userAdded.insert(w);
        else if (op == '-') m_userRemoved.insert(w);
    }
    return true;
}

bool Dictionary::SaveUserOverlay(const std::string& path) const
{
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) return false;

    out << "# RetroEdit user dictionary overlay.\n";
    out << "# Lines starting with '+' add a word; '-' removes a built-in word.\n";
    for (const auto& w : m_userAdded)
        out << '+' << w << '\n';
    for (const auto& w : m_userRemoved)
        out << '-' << w << '\n';
    return out.good();
}

int Dictionary::BuiltinCount() const
{
    return static_cast<int>(m_builtin.size());
}
