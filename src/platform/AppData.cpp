#include "AppData.h"
#include <cstdlib>
#include <direct.h>

std::string UserConfigDir()
{
    char* raw = nullptr;
    size_t len = 0;
    if (_dupenv_s(&raw, &len, "LOCALAPPDATA") != 0 || raw == nullptr)
        return {};

    std::string dir(raw);
    free(raw);
    if (dir.empty()) return {};

    if (dir.back() != '\\' && dir.back() != '/')
        dir.push_back('\\');
    dir += "RetroEdit\\";

    // Best-effort mkdir; existing dir returns -1 with errno=EEXIST which is fine.
    _mkdir(dir.c_str());
    return dir;
}
