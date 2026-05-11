#pragma once
#include <string>
#include <unordered_map>

// Extensible per-file settings store. Persisted alongside the document as a
// sidecar text file ("<documentPath>.retroedit") in a simple "key=value"
// format with '#' comments. Unknown keys round-trip through Load -> Save
// untouched, so settings introduced by future versions survive an edit by
// an older code path.
//
// To add a new per-file setting:
//   1. Call SetBool/SetInt/SetString in Application::CaptureFileSettings.
//   2. Read it back with GetBool/GetInt/GetString (guarded by Has) in
//      Application::ApplyFileSettings.
class FileSettings
{
public:
    bool Load(const std::string& path);
    bool Save(const std::string& path) const;

    bool Empty() const { return m_values.empty(); }
    bool Has(const std::string& key) const;

    void SetString(const std::string& key, const std::string& value);
    void SetBool  (const std::string& key, bool value);
    void SetInt   (const std::string& key, int  value);

    std::string GetString(const std::string& key, const std::string& def = "") const;
    bool        GetBool  (const std::string& key, bool def = false) const;
    int         GetInt   (const std::string& key, int  def = 0) const;

    // Returns "<documentPath>.retroedit", or "" if documentPath is empty.
    static std::string SidecarPath(const std::string& documentPath);

private:
    std::unordered_map<std::string, std::string> m_values;
};
