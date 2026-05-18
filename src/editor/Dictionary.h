#pragma once
#include <string>
#include <unordered_set>

// In-memory spell-check dictionary backed by the embedded built-in word list
// (Dictionary.gen.cpp) plus a per-user overlay of additions and removals
// loaded from / saved to a small text file.
//
// All lookups are case-insensitive; words are stored lowercased.
class Dictionary
{
public:
    Dictionary();  // populates m_builtin from the embedded array (one-time)

    // True iff (word is in user-added OR built-in) AND not in user-removed.
    bool Contains(const std::string& word) const;

    // Adds a word to the user overlay. Returns false if the word is already
    // present (built-in or user-added) AND not currently in the removed set.
    bool AddWord(const std::string& word);

    // Removes a word: drops from user-added if present, or records in
    // user-removed (overriding the built-in). Returns false if the word
    // was already not in the dictionary.
    bool RemoveWord(const std::string& word);

    // Overlay file format (one entry per line):
    //   +word   add this word
    //   -word   remove this word (overrides built-in)
    // Blank lines and lines starting with '#' are ignored.
    bool LoadUserOverlay(const std::string& path);
    bool SaveUserOverlay(const std::string& path) const;

    int BuiltinCount()   const;
    int UserAddedCount() const { return static_cast<int>(m_userAdded.size()); }
    int UserRemovedCount() const { return static_cast<int>(m_userRemoved.size()); }

private:
    std::unordered_set<std::string> m_builtin;
    std::unordered_set<std::string> m_userAdded;
    std::unordered_set<std::string> m_userRemoved;
};
