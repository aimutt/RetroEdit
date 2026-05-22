#pragma once
#include <SDL3/SDL.h>
#include <string>

// Returns "<exe-dir>/assets/<relative>" using SDL_GetBasePath. Build copies
// assets/ next to the exe (see CMakeLists.txt POST_BUILD step) so this is the
// canonical lookup location for runtime data.
inline std::string ResolveAssetPath(const char* relative)
{
    const char* base = SDL_GetBasePath();
    std::string result;
    if (base) result = base;     // SDL_GetBasePath includes a trailing separator
    result += "assets/";
    result += relative;
    return result;
}
