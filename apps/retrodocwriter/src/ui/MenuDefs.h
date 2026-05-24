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
// File=0, Edit=1, Format=2, Search=3, View=4, Page=5, Tools=6, Options=7, Help=8
inline char GetMenuMnemonic(int menuIdx)
{
    static const char mnemonics[] = { 'f', 'e', 'r', 's', 'v', 'p', 't', 'o', 'h' };
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
            { "Print...",     "^P"   },
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
        { "Format", 13, {
            { "Bold",                "^B"     },   // shortcut column shows On/Off at draw time
            { "Italic",              "^I"     },
            { "Underline",           "^U"     },
            { "Strikethrough",       ""       },
            { "Text Color...",       ""       },
            { "Highlight Color...",  ""       },
            { "Insert Page Break",   "^Enter" },
        }},
        { "Search", 22, {
            { "Find...",      "^F"   },
            { "Find Next",    "F6"   },
        }},
        { "View", 30, {
            { "(coming soon)", "" },
        }},
        { "Page", 36, {
            { "Margins...",  ""  },
        }},
        { "Tools", 42, {
            { "Add to Dictionary...",      "" },
            { "Remove from Dictionary...", "" },
            { "",                          "" },
            { "Check Word...",             "" },
        }},
        { "Options", 49, {
            { "Font...",              ""     },
            { "Theme...",             ""     },
            { "Word Wrap",            ""     },   // shortcut column shows On/Off at draw time
            { "Word Count",           ""     },   // shortcut column shows On/Off at draw time
            { "Spell Check",          ""     },   // shortcut column shows On/Off at draw time
            { "Highlight Misspelled", ""     },   // shortcut column shows On/Off at draw time
            { "Show Margins",         ""     },   // shortcut column shows On/Off at draw time
        }},
        { "Help", 58, {
            { "Help",         "F1"   },
            { "",             ""     },
            { "About...",     ""     },
        }},
    };
    return s_menus;
}
