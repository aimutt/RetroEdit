#include "Window.h"

Window::Window(const std::string& title, int width, int height)
{
    if (!SDL_CreateWindowAndRenderer(title.c_str(), width, height,
                                     SDL_WINDOW_RESIZABLE,
                                     &m_window, &m_renderer))
    {
        SDL_Log("SDL_CreateWindowAndRenderer failed: %s", SDL_GetError());
        m_window   = nullptr;
        m_renderer = nullptr;
    }
}

Window::~Window()
{
    if (m_renderer) SDL_DestroyRenderer(m_renderer);
    if (m_window)   SDL_DestroyWindow(m_window);
}

void Window::SetTitle(const std::string& title)
{
    if (m_window)
        SDL_SetWindowTitle(m_window, title.c_str());
}

void Window::SetSize(int width, int height)
{
    if (m_window)
        SDL_SetWindowSize(m_window, width, height);
}
