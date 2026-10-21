#ifndef PYS_CONTENT_TITLE_ASSETS_H
#define PYS_CONTENT_TITLE_ASSETS_H

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

//标题页用到的表现资源:像素字模、字母排布、循环色板和变色节奏。
//这里只声明,定义在 title_assets.cpp,避免每个包含它的编译单元都造一份副本。

//标题大字的像素字模:5 行 x 4 列,'#' 是实心格。只收录标题用到的 6 个字母。
extern const std::string TITLE_GLYPHS[6][5];
//CONSOLE 对应的下标序列。以后换标题文字,改这里就行。
extern const int TITLE_LETTERS[7];
//前景色循环表,相位按字母错开,横着看就是一段流动的渐变。
extern const BYTE TITLE_COLORS[7];
extern const int TITLE_STEPS_MS;    //变色节奏,约 4 个主循环帧
extern const int TITLE_COLOR_COUNT; //TITLE_COLORS 的长度,相位取模用它,别写死数字

#endif
