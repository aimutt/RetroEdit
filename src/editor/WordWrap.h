#pragma once
#include <string>
#include <vector>

// Greedy word wrap. Returns the buffer-column at which each display segment
// begins; the first entry is always 0. A segment never exceeds `width`
// characters; we prefer to break right after the last space that fits, and
// fall back to a hard cut if no space is available within the window.
inline std::vector<int> ComputeWrapStarts(const std::string& line, int width)
{
    std::vector<int> starts;
    starts.push_back(0);

    if (width <= 0)
        return starts;

    const int len = static_cast<int>(line.size());
    int       segStart = 0;

    while (len - segStart > width)
    {
        int searchEnd = segStart + width - 1;
        if (searchEnd >= len) searchEnd = len - 1;

        int breakAt = -1;
        for (int i = searchEnd; i > segStart; --i)
        {
            if (line[i] == ' ') { breakAt = i; break; }
        }

        if (breakAt < 0)
            segStart += width;          // hard break — no space available
        else
            segStart = breakAt + 1;     // next segment starts after the space

        starts.push_back(segStart);
    }

    return starts;
}

inline int CountWrapRows(const std::string& line, int width)
{
    if (line.empty()) return 1;
    return static_cast<int>(ComputeWrapStarts(line, width).size());
}

// Largest segment index whose start is <= col.
inline int WrapSegmentForColumn(const std::vector<int>& starts, int col)
{
    int idx = 0;
    for (int i = 1; i < static_cast<int>(starts.size()); ++i)
    {
        if (starts[i] <= col) idx = i;
        else                  break;
    }
    return idx;
}
