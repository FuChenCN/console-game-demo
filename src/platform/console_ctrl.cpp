#include "platform/console_ctrl.h"

std::atomic<bool> exitRequested{false};

BOOL WINAPI HandleConsoleControl(DWORD event){
    if(event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT)
        return FALSE;
    //回调运行在系统线程中,这里只通知主循环,由主线程统一清理。
    exitRequested.store(true, std::memory_order_relaxed);
    return TRUE;
}
