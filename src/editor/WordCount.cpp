#include "WordCount.h"
#include "TextBuffer.h"
#include <cctype>

int CountWords(const TextBuffer& buffer)
{
    int total = 0;
    int lineCount = buffer.LineCount();
    for (int i = 0; i < lineCount; ++i)
    {
        const std::string& line = buffer.Line(i);
        bool inWord = false;
        for (char c : line)
        {
            bool space = std::isspace(static_cast<unsigned char>(c)) != 0;
            if (!space && !inWord) { ++total; inWord = true; }
            else if (space)        { inWord = false; }
        }
    }
    return total;
}
