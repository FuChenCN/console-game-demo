#include "content/title_assets.h"

const std::string TITLE_GLYPHS[6][5] = {
    {"####", "#   ", "#   ", "#   ", "####"},   //C
    {"####", "#  #", "#  #", "#  #", "####"},   //O
    {"#  #", "## #", "# ##", "#  #", "#  #"},   //N
    {"####", "#   ", "####", "   #", "####"},   //S
    {"#   ", "#   ", "#   ", "#   ", "####"},   //L
    {"####", "#   ", "### ", "#   ", "####"},   //E
};
const int TITLE_LETTERS[7] = {0, 1, 2, 3, 1, 4, 5};

const BYTE TITLE_COLORS[7] = {
    FOREGROUND_RED,
    FOREGROUND_RED | FOREGROUND_GREEN,
    FOREGROUND_GREEN,
    FOREGROUND_GREEN | FOREGROUND_BLUE,
    FOREGROUND_BLUE,
    FOREGROUND_BLUE | FOREGROUND_RED,
    FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,
};
const int TITLE_STEPS_MS = 120;
const int TITLE_COLOR_COUNT = 7;
