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
        return;
    }

    // Pace SDL_RenderPresent to the display's vblank. Without this the main
    // loop spins as fast as the GPU will accept frames (~thousands/sec) and
    // burns 50%+ GPU at idle. 1 = sync to refresh; SDL3 supports negative
    // values for adaptive sync but vblank is plenty for a text editor.
    SDL_SetRenderVSync(m_renderer, 1);
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
