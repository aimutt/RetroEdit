#pragma once
#include <SDL3/SDL.h>
#include <string>

class Window
{
public:
    Window(const std::string& title, int width, int height);
    ~Window();

    Window(const Window&)            = delete;
    Window& operator=(const Window&) = delete;

    bool           IsValid()      const { return m_window != nullptr && m_renderer != nullptr; }
    SDL_Renderer*  GetRenderer()  const { return m_renderer; }
    SDL_Window*    GetWindow()    const { return m_window; }
    void           SetTitle(const std::string& title);
    void           SetSize(int width, int height);

private:
    SDL_Window*   m_window   = nullptr;
    SDL_Renderer* m_renderer = nullptr;
};
