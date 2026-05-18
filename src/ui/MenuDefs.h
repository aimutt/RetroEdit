#pragma once
#include <string>
#include <vector>

struct MenuItemDef
{
    std::string label;      // empty string = separator line
    std::string shortcut;   // display string shown on right side of item
};

struct MenuDef
{
    std::string              label;   // top-level menu name
    int                      barCol;  // column on the menu bar where the label starts
    std::vector<MenuItemDef> items;
};

// Menu mnemonic characters (Alt+letter to open each menu)
// Indices match GetMenuDefs() order:
// File=0, Edit=1, Search=2, View=3, Run=4, Tools=5, Options=6, Help=7
inline char GetMenuMnemonic(int menuIdx)
{
    static const char mnemonics[] = { 'f', 'e', 's', 'v', 'r', 't', 'o', 'h' };
    if (menuIdx < 0 || menuIdx >= static_cast<int>(sizeof(mnemonics)))
        return '\0';
    return mnemonics[menuIdx];
}

inline const std::vector<MenuDef>& GetMenuDefs()
{
    static const std::vector<MenuDef> s_menus = {
        { "File", 1, {
            { "New",          "^N"   },
            { "Open...",      "^O"   },
            { "Save",         "^S"   },
            { "Save As...",   "^S+S" },
            { "",             ""     },
            { "Exit",         "Esc"  },
        }},
        { "Edit", 7, {
            { "Undo",         "^Z"   },
            { "Redo",         "^Y"   },
            { "",             ""     },
            { "Cut",          "^X"   },
            { "Copy",         "^C"   },
            { "Paste",        "^V"   },
            { "",             ""     },
            { "Select All",   "^A"   },
            { "Find...",      "^F"   },
        }},
        { "Search", 13, {
            { "Find...",      "^F"   },
            { "Find Next",    "F6"   },
        }},
        { "View", 21, {
            { "(coming soon)", "" },
        }},
        { "Run", 27, {
            { "(coming soon)", "" },
        }},
        { "Tools", 32, {
            { "Add to Dictionary...",      "" },
            { "Remove from Dictionary...", "" },
            { "",                          "" },
            { "Check Word...",             "" },
        }},
        { "Options", 39, {
            { "Font...",              ""     },
            { "Word Wrap",            ""     },   // shortcut column shows On/Off at draw time
            { "Word Count",           ""     },   // shortcut column shows On/Off at draw time
            { "Spell Check",          ""     },   // shortcut column shows On/Off at draw time
            { "Highlight Misspelled", ""     },   // shortcut column shows On/Off at draw time
        }},
        { "Help", 48, {
            { "Help",         "F1"   },
            { "",             ""     },
            { "About...",     ""     },
        }},
    };
    return s_menus;
}
