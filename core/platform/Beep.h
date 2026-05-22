#pragma once

// Play a short system beep. Used for spell-check audible feedback.
// Wrapped in its own TU so <windows.h> macro pollution stays contained.
void PlayBeep();
