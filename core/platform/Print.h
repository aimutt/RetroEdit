#pragma once
#include "editor/CharStyle.h"
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

    // When useDocumentFont is true, the printer uses fontFamily + pointSize
    // instead of the built-in Courier 10pt fallback. fontFile is the bundled
    // TTF path; the printer registers it via AddFontResourceEx before
    // CreateFont so end-users don't need the font installed system-wide.
    bool              useDocumentFont = false;
    std::string       fontFamily;
    std::string       fontFile;
    int               pointSize       = 10;
    bool              bold            = false;

    // When > 0, the printer uses this chars-per-line value instead of
    // measuring its own character width. Set by Application when WYSIWYG
    // is on so the print wrap matches the on-screen wrap exactly (both
    // are derived from the same DPI-independent ComputeCharsPerLine call).
    // Ignored when `formats` is non-null (the formatted path uses
    // pixel-based wrap with per-char advance).
    int               overrideCharsPerLine = 0;

    // Optional per-character formatting parallel to `buffer`. When non-
    // null, the print path switches GDI fonts per-run to honor style /
    // face / size from each CharFormat. Inherit-sentinel face/size falls
    // back to `fontFamily` / `pointSize` (the document defaults set
    // above). When null, every character prints with the single document
    // font (Phase 2 behavior).
    const std::vector<std::vector<CharFormat>>* formats = nullptr;
};

// Returns the installed printers. First entry is the system default (or the
// list is empty if no printers are installed / the API call failed).
std::vector<std::string> EnumeratePrinters();

// Synchronous; returns a user-facing status string suitable for the status bar
// ("Printed N pages." on success or an error description on failure).
std::string PrintDocument(const TextBuffer& buffer, const PrintRequest& req);
