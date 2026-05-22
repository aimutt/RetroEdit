#include "Print.h"
#include "editor/TextBuffer.h"
#include "render/FontFace.h"
#include "render/FontSettings.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winspool.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>
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
    // still consumes a row on the page. Word-aware: greedily fills up to
    // charsPerLine, then prefers to break at the most recent whitespace
    // inside the segment, falling back to a hard cut when no whitespace fit.
    // Mirrors WysiwygRenderer::WrapLinePx so display and print wrap at the
    // same positions.
    std::vector<std::string> WrapLine(const std::string& src, int charsPerLine)
    {
        std::vector<std::string> out;
        if (charsPerLine <= 0) { out.push_back(src); return out; }
        if (src.empty())       { out.emplace_back();  return out; }

        size_t i = 0;
        const size_t n = src.size();
        while (i < n)
        {
            size_t start   = i;
            size_t lastBrk = std::string::npos;
            size_t taken   = 0;

            while (i < n && taken < static_cast<size_t>(charsPerLine))
            {
                char c = src[i];
                if (c == ' ' || c == '\t') lastBrk = i;
                ++i;
                ++taken;
            }

            if (i < n && lastBrk != std::string::npos && lastBrk > start)
            {
                // Break at the whitespace; skip the space so it doesn't lead
                // the next print line.
                out.emplace_back(src.substr(start, lastBrk - start));
                i = lastBrk + 1;
            }
            else
            {
                // Hard break (no whitespace in the segment, or end of line).
                out.emplace_back(src.substr(start, i - start));
            }
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
                WrapLine(line, charsPerLine).size());
        }
        return total;
    }

    // ---- Formatted print helpers (used when PrintRequest::formats != nullptr) ----

    struct PrintChar
    {
        char    ch;
        HFONT   font;
        int     advance;
        int     lineHeight;
        uint8_t style;
    };

    struct PrintSegment { int startCol; int endCol; int height; };

    // Wrap a single line by pixel-width, mirroring WysiwygRenderer's
    // WrapLinePixels. Breaks at the most recent whitespace when possible.
    std::vector<PrintSegment> WrapLineByPixels(const std::vector<PrintChar>& chars,
                                               int usableW, int emptyLineH)
    {
        std::vector<PrintSegment> out;
        if (chars.empty())
        {
            out.push_back({ 0, 0, emptyLineH });
            return out;
        }
        if (usableW <= 0)
        {
            out.push_back({ 0, static_cast<int>(chars.size()), emptyLineH });
            return out;
        }

        int segStart = 0, xAccum = 0, lastBrk = -1, segH = 0;
        auto emit = [&](int endExcl) {
            int h = segH > 0 ? segH : emptyLineH;
            out.push_back({ segStart, endExcl, h });
        };
        const int n = static_cast<int>(chars.size());
        for (int c = 0; c < n; ++c)
        {
            const PrintChar& cr = chars[c];
            if (xAccum + cr.advance > usableW && c > segStart)
            {
                int breakAt = (lastBrk > segStart) ? lastBrk : c;
                emit(breakAt);
                segStart = (breakAt == c) ? c : (breakAt + 1);
                xAccum = 0;
                segH = 0;
                lastBrk = -1;
                for (int k = segStart; k <= c; ++k)
                {
                    xAccum += chars[k].advance;
                    if (chars[k].lineHeight > segH) segH = chars[k].lineHeight;
                    if (chars[k].ch == ' ' || chars[k].ch == '\t') lastBrk = k;
                }
            }
            else
            {
                xAccum += cr.advance;
                if (cr.lineHeight > segH) segH = cr.lineHeight;
                if (cr.ch == ' ' || cr.ch == '\t') lastBrk = c;
            }
        }
        emit(n);
        return out;
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

// Forward decl: formatted path (per-character font switching).
static std::string PrintDocumentFormatted(const TextBuffer& buffer, const PrintRequest& req);

std::string PrintDocument(const TextBuffer& buffer, const PrintRequest& req)
{
    if (req.formats != nullptr)
        return PrintDocumentFormatted(buffer, req);

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

    // Font selection. When the caller passes useDocumentFont (WYSIWYG mode),
    // register the bundled TTF privately so CreateFont can find it by family
    // name even if it isn't installed system-wide. Falls back to Courier 10pt
    // for plain-text-mode prints.
    const char* family    = "Courier New";
    int         pointSize = 10;
    DWORD       weight    = FW_NORMAL;
    if (req.useDocumentFont && !req.fontFamily.empty())
    {
        family    = req.fontFamily.c_str();
        pointSize = std::max(4, req.pointSize);
        if (req.bold) weight = FW_BOLD;
        if (!req.fontFile.empty())
        {
            // Register once per process per font path.
            static std::vector<std::string> registered;
            if (std::find(registered.begin(), registered.end(), req.fontFile)
                == registered.end())
            {
                if (AddFontResourceExA(req.fontFile.c_str(), FR_PRIVATE, 0) > 0)
                    registered.push_back(req.fontFile);
            }
        }
    }

    HFONT hFont = CreateFontA(
        -MulDiv(pointSize, dpiY, 72),  // negative → cell height in points
        0, 0, 0,
        weight, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, family);
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

    // Use the caller-supplied chars-per-line when set (WYSIWYG mode passes
    // its DPI-independent value so the print wrap matches what the user sees
    // on screen). Otherwise fall back to GDI's measurement.
    int charsPerLine   = (req.overrideCharsPerLine > 0)
                         ? req.overrideCharsPerLine
                         : std::max(1, usableWidth  / charWidth);
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

// ---------------------------------------------------------------------------
// Formatted print path
// ---------------------------------------------------------------------------
//
// When PrintRequest::formats is non-null, the document carries per-character
// CharFormat overrides (style bits + face + size). The single-font path
// above can't honor them, so we use a per-(face, ptSize, styleBits) HFONT
// cache: every printable character is rendered with the GDI font that
// matches its CharFormat. Pixel-based wrap and variable per-segment line
// height mirror WysiwygRenderer's screen layout, so what the user sees on
// screen matches what GDI prints.
static std::string PrintDocumentFormatted(const TextBuffer& buffer,
                                          const PrintRequest& req)
{
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

    const int horzRes = GetDeviceCaps(hdc, HORZRES);
    const int vertRes = GetDeviceCaps(hdc, VERTRES);
    const int dpiX    = GetDeviceCaps(hdc, LOGPIXELSX);
    const int dpiY    = GetDeviceCaps(hdc, LOGPIXELSY);

    auto clampMargin = [](double v) {
        if (v < 0.0) v = 0.0;
        if (v > 5.0) v = 5.0;
        return v;
    };
    const int marginTopPx    = static_cast<int>(clampMargin(req.margins.topIn)    * dpiY);
    const int marginBottomPx = static_cast<int>(clampMargin(req.margins.bottomIn) * dpiY);
    const int marginLeftPx   = static_cast<int>(clampMargin(req.margins.leftIn)   * dpiX);
    const int marginRightPx  = static_cast<int>(clampMargin(req.margins.rightIn)  * dpiX);

    const int usableLeft   = marginLeftPx;
    const int usableTop    = marginTopPx;
    const int usableRight  = horzRes - marginRightPx;
    const int usableBottom = vertRes - marginBottomPx;
    const int usableWidth  = std::max(1, usableRight  - usableLeft);
    const int usableHeight = std::max(1, usableBottom - usableTop);

    // Register every font face that appears in the document (privately —
    // no system install needed). Always include the default face.
    FontFace defaultFace = FontFace::CascadiaMono;
    for (int i = 0; i < FontFaceCount(); ++i)
    {
        if (req.fontFamily == FontFaceFamily(static_cast<FontFace>(i)))
            { defaultFace = static_cast<FontFace>(i); break; }
    }
    const int defaultPt = std::max(4, req.pointSize);

    auto resolveFace = [&](const CharFormat& f) -> FontFace {
        if (f.face == CharFormat::Inherit) return defaultFace;
        if (f.face >= static_cast<uint8_t>(FontFace::Count_)) return defaultFace;
        return static_cast<FontFace>(f.face);
    };
    auto resolveSize = [&](const CharFormat& f) -> int {
        if (f.size == CharFormat::Inherit) return defaultPt;
        if (f.size >= 4) return defaultPt;
        return FontSizePoints(FontSizeAt(static_cast<int>(f.size)));
    };

    // Walk the document, register each unique face's TTF privately.
    static std::vector<std::string> registered; // process-lifetime set
    {
        std::vector<bool> seen(FontFaceCount(), false);
        auto registerFace = [&](FontFace face) {
            int idx = static_cast<int>(face);
            if (idx < 0 || idx >= FontFaceCount() || seen[idx]) return;
            seen[idx] = true;
            // TTF file paths are resolved by Application; PrintRequest
            // only carries the default's file. For other faces we rely
            // on AddFontResourceEx with the same asset directory layout.
            // Application passes the default's path in req.fontFile; the
            // file's directory is the assets/fonts dir, so we just need
            // FontFaceFile(face) appended to that directory.
            if (req.fontFile.empty()) return;
            // Find "fonts/" in req.fontFile and take the dir prefix.
            auto p = req.fontFile.rfind("fonts");
            if (p == std::string::npos) return;
            std::string base = req.fontFile.substr(0, p);
            std::string path = base + FontFaceFile(face);
            if (std::find(registered.begin(), registered.end(), path) == registered.end())
            {
                if (AddFontResourceExA(path.c_str(), FR_PRIVATE, 0) > 0)
                    registered.push_back(path);
            }
        };
        registerFace(defaultFace);
        for (const auto& row : *req.formats)
            for (const auto& f : row)
            {
                if (f.face != CharFormat::Inherit
                    && f.face < static_cast<uint8_t>(FontFace::Count_))
                    registerFace(static_cast<FontFace>(f.face));
            }
    }

    // HFONT cache keyed by (face, ptSize, styleBits).
    std::unordered_map<uint64_t, HFONT> fontCache;
    auto fontFor = [&](FontFace face, int ptSize, uint8_t styleBits) -> HFONT {
        uint64_t key = (static_cast<uint64_t>(face) << 24)
                     | (static_cast<uint64_t>(ptSize & 0xFFFF) << 8)
                     | styleBits;
        auto it = fontCache.find(key);
        if (it != fontCache.end()) return it->second;
        DWORD weight    = (styleBits & 0x01) ? FW_BOLD : FW_NORMAL;
        BOOL  italic    = (styleBits & 0x02) ? TRUE : FALSE;
        BOOL  underline = (styleBits & 0x04) ? TRUE : FALSE;
        BOOL  strikeout = (styleBits & 0x08) ? TRUE : FALSE;
        HFONT f = CreateFontA(
            -MulDiv(ptSize, dpiY, 72),
            0, 0, 0, weight, italic, underline, strikeout,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN,
            FontFaceFamily(face));
        fontCache[key] = f;
        return f;
    };

    HFONT defaultFont = fontFor(defaultFace, defaultPt, 0);
    if (!defaultFont)
    {
        for (auto& kv : fontCache) if (kv.second) DeleteObject(kv.second);
        DeleteDC(hdc);
        ClosePrinter(hPrinter);
        return FormatLastError("CreateFont failed");
    }
    HGDIOBJ oldFont = SelectObject(hdc, defaultFont);
    TEXTMETRICA tmDefault{};
    GetTextMetricsA(hdc, &tmDefault);
    const int defaultLineHeight = tmDefault.tmHeight + tmDefault.tmExternalLeading;

    // Pre-pass: build PrintChar per buffer line. Switching SelectObject is
    // amortized — most documents have only a few distinct (face, size,
    // style) combos, so the metric lookups stay cheap.
    std::vector<std::vector<PrintChar>> chars(buffer.LineCount());
    HFONT lastSelected = defaultFont;
    int   lastLineHeight = defaultLineHeight;
    for (int li = 0; li < buffer.LineCount(); ++li)
    {
        const std::string& line = buffer.Line(li);
        chars[li].reserve(line.size());
        for (size_t c = 0; c < line.size(); ++c)
        {
            CharFormat f{};
            if (li < static_cast<int>(req.formats->size())
                && c < (*req.formats)[li].size())
                f = (*req.formats)[li][c];
            FontFace face = resolveFace(f);
            int      pt   = resolveSize(f);
            HFONT    font = fontFor(face, pt, f.style);
            if (font != lastSelected)
            {
                SelectObject(hdc, font);
                TEXTMETRICA tm{};
                GetTextMetricsA(hdc, &tm);
                lastLineHeight = tm.tmHeight + tm.tmExternalLeading;
                lastSelected = font;
            }
            char ch = line[c];
            INT w = 0;
            int adv = 0;
            if (GetCharWidth32A(hdc, static_cast<UINT>(static_cast<unsigned char>(ch)),
                                       static_cast<UINT>(static_cast<unsigned char>(ch)), &w))
                adv = w;
            else
            {
                SIZE sz{};
                GetTextExtentPoint32A(hdc, &ch, 1, &sz);
                adv = sz.cx;
            }
            chars[li].push_back({ ch, font, adv, lastLineHeight, f.style });
        }
    }

    // Wrap each line by pixel width and collect total page count.
    std::vector<std::vector<PrintSegment>> segments(buffer.LineCount());
    for (int li = 0; li < buffer.LineCount(); ++li)
        segments[li] = WrapLineByPixels(chars[li], usableWidth, defaultLineHeight);

    // Greedy pagination: count pages and pre-compute per-segment page+y.
    struct PlacedSeg { int li; int s; int page; int yInPage; };
    std::vector<PlacedSeg> placed;
    const int footerH = defaultLineHeight; // bottom row reserved for footer
    const int usableForText = std::max(1, usableHeight - footerH);
    int curPage = 1;
    int yInPage = 0;
    for (int li = 0; li < buffer.LineCount(); ++li)
    {
        for (size_t s = 0; s < segments[li].size(); ++s)
        {
            int h = segments[li][s].height;
            if (yInPage + h > usableForText && yInPage > 0)
            {
                ++curPage;
                yInPage = 0;
            }
            placed.push_back({ li, static_cast<int>(s), curPage, yInPage });
            yInPage += h;
        }
    }
    const int totalPages = std::max(1, curPage);

    const int firstPage = req.allPages ? 1 : std::max(1, req.pageFrom);
    const int lastPage  = req.allPages ? totalPages
                                       : std::min(totalPages, std::max(firstPage, req.pageTo));

    DOCINFOA di{};
    di.cbSize = sizeof(di);
    std::string docName = req.documentName.empty() ? "RetroDocWriter Document"
                                                   : req.documentName;
    di.lpszDocName = docName.c_str();

    if (StartDocA(hdc, &di) <= 0)
    {
        std::string msg = FormatLastError("StartDoc failed");
        SelectObject(hdc, oldFont);
        for (auto& kv : fontCache) if (kv.second) DeleteObject(kv.second);
        DeleteDC(hdc);
        ClosePrinter(hPrinter);
        return msg;
    }

    int pagesEmitted = 0;
    int activePage   = -1;

    auto emitFooter = [&](int pageNum) {
        SelectObject(hdc, defaultFont);
        std::string left  = docName;
        std::string right = "Page " + std::to_string(pageNum) + " of " + std::to_string(totalPages);
        const int footerY = usableTop + usableForText;
        TextOutA(hdc, usableLeft, footerY,
                 left.c_str(), static_cast<int>(left.size()));
        SIZE rs{};
        GetTextExtentPoint32A(hdc, right.c_str(), static_cast<int>(right.size()), &rs);
        TextOutA(hdc, usableRight - rs.cx, footerY,
                 right.c_str(), static_cast<int>(right.size()));
    };

    for (const PlacedSeg& ps : placed)
    {
        if (ps.page < firstPage) continue;
        if (ps.page > lastPage)  break;

        if (activePage != ps.page)
        {
            if (activePage > 0)
            {
                emitFooter(activePage);
                EndPage(hdc);
                ++pagesEmitted;
            }
            if (StartPage(hdc) <= 0) break;
            activePage = ps.page;
        }

        const auto& seg = segments[ps.li][ps.s];
        const auto& lineChars = chars[ps.li];
        int x = usableLeft;
        int y = usableTop + ps.yInPage;
        // Group consecutive chars by font for fewer SelectObject calls.
        int groupStart = seg.startCol;
        while (groupStart < seg.endCol)
        {
            HFONT groupFont = lineChars[groupStart].font;
            int groupEnd = groupStart + 1;
            while (groupEnd < seg.endCol && lineChars[groupEnd].font == groupFont)
                ++groupEnd;
            SelectObject(hdc, groupFont);
            std::string text;
            text.reserve(static_cast<size_t>(groupEnd - groupStart));
            for (int c = groupStart; c < groupEnd; ++c)
                text.push_back(lineChars[c].ch);
            TextOutA(hdc, x, y, text.c_str(), static_cast<int>(text.size()));
            for (int c = groupStart; c < groupEnd; ++c)
                x += lineChars[c].advance;
            groupStart = groupEnd;
        }
    }
    if (activePage > 0)
    {
        emitFooter(activePage);
        EndPage(hdc);
        ++pagesEmitted;
    }

    EndDoc(hdc);
    SelectObject(hdc, oldFont);
    for (auto& kv : fontCache) if (kv.second) DeleteObject(kv.second);
    DeleteDC(hdc);
    ClosePrinter(hPrinter);

    if (pagesEmitted == 0)
        return "Nothing printed (page range produced no pages).";

    std::string msg = "Printed " + std::to_string(pagesEmitted) + " page";
    if (pagesEmitted != 1) msg += "s";
    msg += ".";
    if (req.copies > 1)
        msg += " (" + std::to_string(req.copies) + " copies)";
    return msg;
}
