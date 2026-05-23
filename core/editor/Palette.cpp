#include "Palette.h"

namespace
{
    constexpr Color kPalette[Palette::kCount] = {
        {  0,   0,   0, 255},   // 0  Black
        {128,   0,   0, 255},   // 1  Dark Red
        {  0, 128,   0, 255},   // 2  Dark Green
        {128, 128,   0, 255},   // 3  Dark Yellow
        {  0,   0, 128, 255},   // 4  Dark Blue
        {128,   0, 128, 255},   // 5  Dark Magenta
        {  0, 128, 128, 255},   // 6  Dark Cyan
        {192, 192, 192, 255},   // 7  Light Gray
        {128, 128, 128, 255},   // 8  Dark Gray
        {255,   0,   0, 255},   // 9  Red
        {  0, 255,   0, 255},   // 10 Green
        {255, 255,   0, 255},   // 11 Yellow
        {  0,   0, 255, 255},   // 12 Blue
        {255,   0, 255, 255},   // 13 Magenta
        {  0, 255, 255, 255},   // 14 Cyan
        {255, 255, 255, 255},   // 15 White
    };

    constexpr const char* kNames[Palette::kCount] = {
        "Black",   "Dark Red",  "Dark Green",  "Dark Yellow",
        "Dark Blue","Dark Magenta","Dark Cyan","Light Gray",
        "Dark Gray","Red",       "Green",       "Yellow",
        "Blue",    "Magenta",   "Cyan",        "White",
    };
}

namespace Palette
{
Color ColorAt(uint8_t index)
{
    if (index >= kCount) return {0, 0, 0, 255};
    return kPalette[index];
}

const char* NameAt(uint8_t index)
{
    if (index >= kCount) return "?";
    return kNames[index];
}

uint8_t NearestIndex(Color rgb)
{
    int best   = 0;
    int bestD2 = -1;
    for (int i = 0; i < kCount; ++i)
    {
        int dr = static_cast<int>(rgb.r) - static_cast<int>(kPalette[i].r);
        int dg = static_cast<int>(rgb.g) - static_cast<int>(kPalette[i].g);
        int db = static_cast<int>(rgb.b) - static_cast<int>(kPalette[i].b);
        int d2 = dr * dr + dg * dg + db * db;
        if (bestD2 < 0 || d2 < bestD2)
        {
            bestD2 = d2;
            best   = i;
        }
    }
    return static_cast<uint8_t>(best);
}
} // namespace Palette
