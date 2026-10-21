#ifndef PYS_PLATFORM_CONSOLE_CTRL_H
#define PYS_PLATFORM_CONSOLE_CTRL_H

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>

//Ctrl+C / Ctrl+Break 的落地标志。回调只负责置位,清理统一由主线程做。
extern std::atomic<bool> exitRequested;

//交给 SetConsoleCtrlHandler 的回调。放在平台层,游戏逻辑不碰它。
BOOL WINAPI HandleConsoleControl(DWORD event);

#endif
