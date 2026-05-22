#pragma once

// Runtime layout — row positions depend on SCREEN_ROWS which is computed at startup
// from the actual display height so the window never exceeds 90% of the screen.
struct Layout
{
    int ROW_MENUBAR;
    int ROW_SEP_TOP;
    int ROW_EDITOR_FIRST;
    int ROW_EDITOR_LAST;
    int ROW_STATUS;
    int ROW_SEP_BOT;
    int ROW_FKEYS;
    int EDITOR_ROWS;
    int SCREEN_ROWS;

    Layout() : Layout(58) {}   // fallback used before display query

    explicit Layout(int screenRows)
        : ROW_MENUBAR     (0)
        , ROW_SEP_TOP     (1)
        , ROW_EDITOR_FIRST(2)
        , ROW_EDITOR_LAST (screenRows - 4)
        , ROW_STATUS      (screenRows - 3)
        , ROW_SEP_BOT     (screenRows - 2)
        , ROW_FKEYS       (screenRows - 1)
        , EDITOR_ROWS     (ROW_EDITOR_LAST - ROW_EDITOR_FIRST + 1)
        , SCREEN_ROWS     (screenRows)
    {}
};
