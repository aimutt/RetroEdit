#pragma once

class TextBuffer;

// Count whitespace-separated word runs across every line in the buffer.
int CountWords(const TextBuffer& buffer);
