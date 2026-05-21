#include "Print.h"
#include "editor/TextBuffer.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winspool.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    std::string DefaultPrinterName()
    {
        DWORD len = 0;
        GetDefaultPrinterA(nullptr, &len);
        if (len == 0) return {};
        std::string out(len, '\0');
        if (!GetDefaultPrinterA(out.data(), &len)) return {};
        if (!out.empty() && out.back() == '\0') out.pop_back();
        return out;
    }

    std::string FormatLastError(const char* prefix)
    {
        DWORD err = GetLastError();
        char* msg = nullptr;
        FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       reinterpret_cast<LPSTR>(&msg), 0, nullptr);
        std::string out = prefix ? prefix : "Print error";
        if (msg)
        {
            out += ": ";
            out += msg;
            // Strip trailing CRLF FormatMessage tends to append.
            while (!out.empty() && (out.back() == '\r' || out.back() == '\n' || out.back() == ' '))
                out.pop_back();
            LocalFree(msg);
        }
        return out;
    }

    // Wrap a single source line into print-line segments, each of length
    // <= charsPerLine. Empty source line produces one empty segment so it
    // still consumes a row on the page.
    std::vector<std::string> WrapLine(const std::string& src, int charsPerLine)
    {
        std::vector<std::string> out;
        if (charsPerLine <= 0) { out.push_back(src); return out; }
        if (src.empty())       { out.emplace_back();  return out; }
        size_t i = 0;
        while (i < src.size())
        {
            size_t take = std::min<size_t>(static_cast<size_t>(charsPerLine),
                                           src.size() - i);
            out.emplace_back(src, i, take);
            i += take;
        }
        return out;
    }

    int CountTotalPrintLines(const TextBuffer& buf, int charsPerLine)
    {
        int total = 0;
        int n = buf.LineCount();
        for (int i = 0; i < n; ++i)
        {
            const std::string& line = buf.Line(i);
            if (line.empty())                  total += 1;
            else if (charsPerLine <= 0)        total += 1;
            else                               total += static_cast<int>(
                (line.size() + charsPerLine - 1) / charsPerLine);
        }
        return total;
    }
}

std::vector<std::string> EnumeratePrinters()
{
    std::vector<std::string> out;

    // Two passes through EnumPrintersA — sizing then fill.
    DWORD needed = 0, returned = 0;
    EnumPrintersA(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS,
                  nullptr, 2, nullptr, 0, &needed, &returned);
    if (needed > 0)
    {
        std::vector<unsigned char> buf(needed);
        if (EnumPrintersA(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS,
                          nullptr, 2, buf.data(), needed, &needed, &returned))
        {
            const PRINTER_INFO_2A* info = reinterpret_cast<const PRINTER_INFO_2A*>(buf.data());
            for (DWORD i = 0; i < returned; ++i)
                if (info[i].pPrinterName) out.emplace_back(info[i].pPrinterName);
        }
    }

    // Hoist the default printer to the front.
    std::string def = DefaultPrinterName();
    if (!def.empty())
    {
        auto it = std::find(out.begin(), out.end(), def);
        if (it != out.end())
        {
            std::rotate(out.begin(), it, it + 1);
        }
        else
        {
            out.insert(out.begin(), def);
        }
    }
    return out;
}

std::string PrintDocument(const TextBuffer& buffer, const PrintRequest& req)
{
    // Resolve printer name (default to first installed).
    std::string printerName = req.printerName;
    if (printerName.empty())
    {
        auto list = EnumeratePrinters();
        if (list.empty()) return "No printers installed.";
        printerName = list.front();
    }

    HANDLE hPrinter = nullptr;
    if (!OpenPrinterA(const_cast<char*>(printerName.c_str()), &hPrinter, nullptr))
        return FormatLastError("OpenPrinter failed");

    // Resolve DEVMODE — size, fill, override orientation/copies, merge.
    LONG devSize = DocumentPropertiesA(nullptr, hPrinter,
                                       const_cast<char*>(printerName.c_str()),
                                       nullptr, nullptr, 0);
    if (devSize < 0)
    {
        ClosePrinter(hPrinter);
        return FormatLastError("DocumentProperties (size) failed");
    }
    std::vector<unsigned char> devBuf(static_cast<size_t>(devSize));
    DEVMODEA* devmode = reinterpret_cast<DEVMODEA*>(devBuf.data());
    if (DocumentPropertiesA(nullptr, hPrinter,
                            const_cast<char*>(printerName.c_str()),
                            devmode, nullptr, DM_OUT_BUFFER) < 0)
    {
        ClosePrinter(hPrinter);
        return FormatLastError("DocumentProperties (read) failed");
    }
    devmode->dmFields    |= DM_ORIENTATION | DM_COPIES;
    devmode->dmOrientation = (req.orientation == PrintOrientation::Landscape)
                             ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
    devmode->dmCopies      = static_cast<short>(std::max(1, req.copies));
    if (DocumentPropertiesA(nullptr, hPrinter,
                            const_cast<char*>(printerName.c_str()),
                            devmode, devmode,
                            DM_IN_BUFFER | DM_OUT_BUFFER) < 0)
    {
        ClosePrinter(hPrinter);
        return FormatLastError("DocumentProperties (merge) failed");
    }

    HDC hdc = CreateDCA("WINSPOOL", printerName.c_str(), nullptr, devmode);
    if (!hdc)
    {
        ClosePrinter(hPrinter);
        return FormatLastError("CreateDC failed");
    }

    // Pixel metrics from the device.
    const int horzRes   = GetDeviceCaps(hdc, HORZRES);
    const int vertRes   = GetDeviceCaps(hdc, VERTRES);
    const int dpiX      = GetDeviceCaps(hdc, LOGPIXELSX);
    const int dpiY      = GetDeviceCaps(hdc, LOGPIXELSY);

    auto clampMargin = [](double v) {
        if (v < 0.0) v = 0.0;
        if (v > 5.0) v = 5.0;
        return v;
    };
    const int marginTopPx    = static_cast<int>(clampMargin(req.margins.topIn)    * dpiY);
    const int marginBottomPx = static_cast<int>(clampMargin(req.margins.bottomIn) * dpiY);
    const int marginLeftPx   = static_cast<int>(clampMargin(req.margins.leftIn)   * dpiX);
    const int marginRightPx  = static_cast<int>(clampMargin(req.margins.rightIn)  * dpiX);

    // Build the 10pt Courier New font for the printer DC.
    HFONT hFont = CreateFontA(
        -MulDiv(10, dpiY, 72),  // negative → cell height in points
        0, 0, 0,
        FW_NORMAL, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Courier New");
    if (!hFont)
    {
        DeleteDC(hdc);
        ClosePrinter(hPrinter);
        return FormatLastError("CreateFont failed");
    }
    HGDIOBJ oldFont = SelectObject(hdc, hFont);

    TEXTMETRICA tm{};
    GetTextMetricsA(hdc, &tm);
    const int lineHeight = tm.tmHeight + tm.tmExternalLeading;

    SIZE charSize{};
    GetTextExtentPoint32A(hdc, "M", 1, &charSize);
    const int charWidth = std::max(1L, charSize.cx);

    // Usable area + per-page line capacity.
    const int usableLeft   = marginLeftPx;
    const int usableTop    = marginTopPx;
    const int usableRight  = horzRes - marginRightPx;
    const int usableBottom = vertRes - marginBottomPx;
    int       usableWidth  = std::max(0, usableRight  - usableLeft);
    int       usableHeight = std::max(0, usableBottom - usableTop);

    int charsPerLine   = std::max(1, usableWidth  / charWidth);
    int linesPerPage   = std::max(1, (usableHeight - lineHeight) / lineHeight); // -1 for footer
    // Reserve the very bottom row for the footer.
    const int footerY = usableTop + linesPerPage * lineHeight;

    // Total pages (consider page range so the footer denominator matches what
    // the user requested).
    const int totalPrintLinesAll = CountTotalPrintLines(buffer, charsPerLine);
    const int totalPagesAll      = std::max(1, (totalPrintLinesAll + linesPerPage - 1) / linesPerPage);
    int firstPage = req.allPages ? 1 : std::max(1, req.pageFrom);
    int lastPage  = req.allPages ? totalPagesAll
                                 : std::min(totalPagesAll, std::max(firstPage, req.pageTo));

    DOCINFOA di{};
    di.cbSize    = sizeof(di);
    std::string docName = req.documentName.empty() ? "RetroEdit Document" : req.documentName;
    di.lpszDocName = docName.c_str();

    if (StartDocA(hdc, &di) <= 0)
    {
        std::string msg = FormatLastError("StartDoc failed");
        SelectObject(hdc, oldFont);
        DeleteObject(hFont);
        DeleteDC(hdc);
        ClosePrinter(hPrinter);
        return msg;
    }

    // Print loop: walk the buffer, wrap each line, paginate, emit only pages
    // inside [firstPage, lastPage]. NOTE: dmCopies (DEVMODE) tells the driver
    // to spool the document N times — we don't loop here for copies.
    int  currentPage = 1;
    int  lineOnPage  = 0;
    bool pageStarted = false;
    int  pagesEmitted = 0;

    auto emitFooter = [&](int pageNum) {
        std::string left  = docName;
        std::string right = "Page " + std::to_string(pageNum) + " of " + std::to_string(totalPagesAll);
        TextOutA(hdc, usableLeft, footerY,
                 left.c_str(), static_cast<int>(left.size()));
        SIZE rs{};
        GetTextExtentPoint32A(hdc, right.c_str(), static_cast<int>(right.size()), &rs);
        TextOutA(hdc, usableRight - rs.cx, footerY,
                 right.c_str(), static_cast<int>(right.size()));
    };

    auto beginPageIfNeeded = [&]() {
        if (pageStarted) return true;
        if (currentPage < firstPage || currentPage > lastPage) return false;
        if (StartPage(hdc) <= 0) return false;
        // Font selection survives across pages on most drivers but reselecting
        // is the safe, portable choice.
        SelectObject(hdc, hFont);
        pageStarted = true;
        return true;
    };

    auto endPageIfStarted = [&]() {
        if (!pageStarted) return;
        emitFooter(currentPage);
        EndPage(hdc);
        ++pagesEmitted;
        pageStarted = false;
    };

    auto advanceLine = [&]() {
        ++lineOnPage;
        if (lineOnPage >= linesPerPage)
        {
            endPageIfStarted();
            lineOnPage = 0;
            ++currentPage;
        }
    };

    const int lineCount = buffer.LineCount();
    for (int li = 0; li < lineCount; ++li)
    {
        auto segments = WrapLine(buffer.Line(li), charsPerLine);
        for (const auto& seg : segments)
        {
            if (currentPage > lastPage) break;
            if (currentPage >= firstPage && beginPageIfNeeded())
            {
                int y = usableTop + lineOnPage * lineHeight;
                TextOutA(hdc, usableLeft, y,
                         seg.c_str(), static_cast<int>(seg.size()));
            }
            advanceLine();
        }
        if (currentPage > lastPage) break;
    }
    endPageIfStarted();

    EndDoc(hdc);

    SelectObject(hdc, oldFont);
    DeleteObject(hFont);
    DeleteDC(hdc);
    ClosePrinter(hPrinter);

    if (pagesEmitted == 0)
        return "Nothing printed (page range produced no pages).";

    std::string msg = "Printed " + std::to_string(pagesEmitted) + " page";
    if (pagesEmitted != 1) msg += "s";
    msg += ".";
    if (req.copies > 1)
    {
        msg += " (" + std::to_string(req.copies) + " copies)";
    }
    return msg;
}
