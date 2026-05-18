#pragma once
#include <string>

// Returns the user-config directory for RetroEdit, creating it if missing.
// On Windows: "%LOCALAPPDATA%\RetroEdit\" (with trailing separator).
// Returns an empty string if the directory cannot be located or created;
// callers must treat that as "persistence disabled" and no-op gracefully.
std::string UserConfigDir();
