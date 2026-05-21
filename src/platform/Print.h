#pragma once
#include <string>
#include <vector>

class TextBuffer;

enum class PrintOrientation { Portrait, Landscape };

struct PrintMargins
{
    double topIn    = 0.5;
    double bottomIn = 0.5;
    double leftIn   = 0.75;
    double rightIn  = 0.75;
};

struct PrintRequest
{
    std::string       printerName;             // empty = first from EnumeratePrinters()
    int               copies      = 1;
    bool              allPages    = true;
    int               pageFrom    = 1;         // honored only when allPages == false
    int               pageTo      = 1;         // honored only when allPages == false
    PrintOrientation  orientation = PrintOrientation::Portrait;
    PrintMargins      margins;
    std::string       documentName;            // used in the page footer
};

// Returns the installed printers. First entry is the system default (or the
// list is empty if no printers are installed / the API call failed).
std::vector<std::string> EnumeratePrinters();

// Synchronous; returns a user-facing status string suitable for the status bar
// ("Printed N pages." on success or an error description on failure).
std::string PrintDocument(const TextBuffer& buffer, const PrintRequest& req);
