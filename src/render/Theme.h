#pragma once
#include "Color.h"

struct Theme
{
    Color background        {  0,  12,   0, 255};
    Color normalText        { 80, 255, 120, 255};
    Color dimText           { 40, 160,  70, 255};
    Color brightText        {170, 255, 190, 255};
    Color reverseForeground {  0,  20,   0, 255};
    Color reverseBackground {100, 255, 130, 255};
    Color border            { 80, 220, 100, 255};
};
