#pragma once
#include "render/Color.h"
#include <cstdint>

// Fixed 16-color palette used for per-character text color in
// RetroDocWriter. Indices match the classic CGA / RTF default ordering
// so the writer's \colortbl emits them in the conventional sequence and
// the reader can map third-party \cf indices back to ours by
// nearest-color match.
//
// Index 0 = Black so that a default \cf0 (or "auto") maps to a sensible
// foreground on a white-paper printout. Inherit-sentinel chars
// (CharFormat::color == Inherit) bypass the palette entirely and pick
// up the active Theme::normalText instead — so changing the screen
// theme moves unstyled text but leaves explicitly-colored text in place.
namespace Palette
{
    inline constexpr int kCount = 16;

    Color       ColorAt(uint8_t index);   // OOB → black
    const char* NameAt (uint8_t index);   // OOB → "?"

    // Returns the palette index closest to `rgb` (Euclidean distance).
    // Used by the RTF reader to fold third-party \colortbl entries into
    // our fixed palette.
    uint8_t NearestIndex(Color rgb);
}
