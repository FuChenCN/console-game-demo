#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>
using namespace std;

atomic<bool> exitRequested{false};

//商店商品条目。available=false 的是预留空位:占着列表的一行,但还没实装商品。
struct ShopItem{
    const char* name;
    int price;
    int owned;
    bool available;
};

BOOL WINAPI HandleConsoleControl(DWORD event){
    if(event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT)
        return FALSE;
    //回调运行在系统线程中,这里只通知主循环,由主线程统一清理。
    exitRequested.store(true, memory_order_relaxed);
    return TRUE;
}

int main(){
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
    DWORD originalInputMode;
    if(!GetConsoleScreenBufferInfo(hConsole, &consoleInfo) ||
       !GetConsoleMode(hInput, &originalInputMode)){
        fputs("PYS requires an interactive Windows console.\n", stderr);
        return 1;
    }
    if(!SetConsoleCtrlHandler(HandleConsoleControl, TRUE)){
        fputs("PYS could not configure console exit handling.\n", stderr);
        return 1;
    }
    DWORD inputMode = (originalInputMode | ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT |
                      ENABLE_PROCESSED_INPUT) &
                      ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_QUICK_EDIT_MODE |
                        ENABLE_VIRTUAL_TERMINAL_INPUT);
    if(!SetConsoleMode(hInput, inputMode)){
        SetConsoleCtrlHandler(HandleConsoleControl, FALSE);
        fputs("PYS could not configure console input.\n", stderr);
        return 1;
    }
    CONSOLE_CURSOR_INFO originalCursor;
    bool restoreCursor = GetConsoleCursorInfo(hConsole, &originalCursor) != FALSE;
    CONSOLE_CURSOR_INFO hiddenCursor = {1, FALSE};
    SetConsoleCursorInfo(hConsole, &hiddenCursor);

    const int MAP_W = 128;
    const int MAP_H = 32;
    const int RESOURCE_RATE = 6;
    bool resource[MAP_H][MAP_W] = {};
    int remainResource = 0;
    srand(static_cast<unsigned int>(time(nullptr)));
    for(int row = 0; row < MAP_H; row++){
        for(int column = 0; column < MAP_W; column++){
            if(rand() % 100 < RESOURCE_RATE){
                resource[row][column] = true;
                remainResource++;
            }
        }
    }

    //画面坐标相对可见窗口,不修改终端尺寸或窗口样式。
    SMALL_RECT viewport = {-1, -1, -1, -1};
    COORD bufferSize = {};
    CHAR_INFO blank = {};
    blank.Char.AsciiChar = ' ';
    blank.Attributes = consoleInfo.wAttributes;
    vector<CHAR_INFO> frame;
    vector<CHAR_INFO> displayed;
    bool needsPresent = false;
    bool forceRedraw = true;
    auto setCell = [&](int column, int row, char value){
        if(column >= 0 && column < bufferSize.X && row >= 0 && row < bufferSize.Y)
            frame[row * bufferSize.X + column].Char.AsciiChar = value;
    };
    auto writeText = [&](int column, int row, const string& text, int width){
        int count = min(width, static_cast<int>(text.size()));
        for(int offset = 0; offset < count; offset++)
            setCell(column + offset, row, text[offset]);
    };
    auto present = [&](){
        if(!needsPresent)
            return;
        SMALL_RECT dirty = {bufferSize.X, bufferSize.Y, -1, -1};
        if(forceRedraw){
            dirty = SMALL_RECT{0, 0, static_cast<SHORT>(bufferSize.X - 1),
                                   static_cast<SHORT>(bufferSize.Y - 1)};
        }
        else{
            //比较最终画面,避免把合成过程中的临时变化提交给终端。
            for(int row = 0; row < bufferSize.Y; row++){
                for(int column = 0; column < bufferSize.X; column++){
                    int index = row * bufferSize.X + column;
                    if(frame[index].Char.AsciiChar == displayed[index].Char.AsciiChar)
                        continue;
                    dirty.Left = static_cast<SHORT>(min<int>(dirty.Left, column));
                    dirty.Right = static_cast<SHORT>(max<int>(dirty.Right, column));
                    dirty.Top = static_cast<SHORT>(min<int>(dirty.Top, row));
                    dirty.Bottom = static_cast<SHORT>(max<int>(dirty.Bottom, row));
                }
            }
        }
        if(dirty.Left > dirty.Right || dirty.Top > dirty.Bottom){
            needsPresent = false;
            return;
        }
        COORD source = {dirty.Left, dirty.Top};
        SMALL_RECT target = {
            static_cast<SHORT>(viewport.Left + dirty.Left),
            static_cast<SHORT>(viewport.Top + dirty.Top),
            static_cast<SHORT>(viewport.Left + dirty.Right),
            static_cast<SHORT>(viewport.Top + dirty.Bottom)
        };
        SMALL_RECT written = target;
        if(WriteConsoleOutputA(hConsole, frame.data(), bufferSize, source, &written) &&
           written.Left == target.Left && written.Top == target.Top &&
           written.Right == target.Right && written.Bottom == target.Bottom){
            displayed = frame;
            needsPresent = false;
            forceRedraw = false;
        }
        else{
            //窗口可能在查询尺寸和提交之间变化,下一帧保留并重试。
            forceRedraw = true;
        }
    };

    int x = 10;
    int y = 10;
    int resourceCount = 0;
    int scene = 0;
    //商品表驱动商店的画面和购买:加新商品只需在这里多填一行,选择、渲染、扣费都跟着走。
    //后面三项是留出来的空位(available=false),给后续 commit 填实际商品。
    vector<ShopItem> shopItems = {
        {"Auto Robot", 100, 0, true},
        {"", 0, 0, false},
        {"", 0, 0, false},
        {"", 0, 0, false},
    };
    int shopSelection = 0;                          //商店光标指向的商品下标
    string shopNotice = "Pick an item, Enter to buy";  //商店底部反馈行
    int exitCode = 0;
    array<INPUT_RECORD, 64> inputRecords;
    vector<KEY_EVENT_RECORD> keys;
    while(!exitRequested.load(memory_order_relaxed)){
        //只消费本终端的按键,不读取其他窗口中的全局键盘状态。
        keys.clear();
        DWORD pending = 0;
        if(!GetNumberOfConsoleInputEvents(hInput, &pending)){
            exitCode = 1;
            break;
        }
        while(pending > 0 && !exitRequested.load(memory_order_relaxed)){
            DWORD read = 0;
            DWORD count = min<DWORD>(pending, static_cast<DWORD>(inputRecords.size()));
            if(!ReadConsoleInputW(hInput, inputRecords.data(), count, &read) || read == 0){
                exitCode = 1;
                break;
            }
            pending -= read;
            for(DWORD index = 0; index < read; index++){
                const auto& record = inputRecords[index];
                //焦点、鼠标、窗口事件均已消费,不把它们当作退出指令。
                if(record.EventType != KEY_EVENT)
                    continue;
                const auto& key = record.Event.KeyEvent;
                if(key.bKeyDown && !(key.dwControlKeyState &
                   (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED | LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)))
                    keys.push_back(key);
            }
        }
        if(exitCode != 0 || any_of(keys.begin(), keys.end(), [](const KEY_EVENT_RECORD& key){
            return key.wVirtualKeyCode == 'Q';
        }))
            break;
        if(!GetConsoleScreenBufferInfo(hConsole, &consoleInfo)){
            if(GetLastError() == ERROR_INVALID_HANDLE){
                exitCode = 1;
                break;
            }
            Sleep(30);
            continue;
        }
        const SMALL_RECT current = consoleInfo.srWindow;
        bool resized = current.Left != viewport.Left || current.Top != viewport.Top ||
                       current.Right != viewport.Right || current.Bottom != viewport.Bottom;
        if(resized){
            viewport = current;
            bufferSize.X = viewport.Right - viewport.Left + 1;
            bufferSize.Y = viewport.Bottom - viewport.Top + 1;
            frame.assign(static_cast<size_t>(bufferSize.X) * bufferSize.Y, blank);
            displayed = frame;
            forceRedraw = true;
            needsPresent = true;
        }
        int width = bufferSize.X;
        int height = bufferSize.Y;
        bool canPlay = width >= 32 && height >= 12;
        int oldScene = scene;
        int oldX = x;
        int oldY = y;
        int oldResourceCount = resourceCount;
        int oldRemainResource = remainResource;
        //商店里光标位置和反馈文字的变化,不属于上面那几个数值比较,单独用标志位记。
        bool shopChanged = false;
        for(const auto& key : keys){
            int code = key.wVirtualKeyCode;
            int repeat = max<int>(1, key.wRepeatCount);
            if(code == VK_RIGHT) scene = 1;
            if(code == VK_LEFT) scene = 0;
            if(!canPlay)
                continue;
            if(scene == 0){
                //采集场景:WASD 移动,E 或空格采掉脚下的资源。
                if(code == 'W') y = max(0, y - repeat);
                if(code == 'S') y = min(MAP_H - 1, y + repeat);
                if(code == 'A') x = max(0, x - repeat);
                if(code == 'D') x = min(MAP_W - 1, x + repeat);
                if((code == 'E' || code == VK_SPACE) && resource[y][x]){
                    resource[y][x] = false;
                    remainResource--;
                    resourceCount++;
                }
                continue;
            }
            //商店场景:光标在商品表上移动,Enter 买下光标指向的那一项。
            //W/S 和上下方向键都能移动光标,取模实现到顶后绕回。
            int itemCount = static_cast<int>(shopItems.size());
            if(code == 'W' || code == VK_UP){
                shopSelection = (shopSelection + itemCount - 1) % itemCount;
                shopChanged = true;
            }
            if(code == 'S' || code == VK_DOWN){
                shopSelection = (shopSelection + 1) % itemCount;
                shopChanged = true;
            }
            if(code == VK_RETURN){
                ShopItem& item = shopItems[shopSelection];
                if(!item.available){
                    //空位不可购买,给出提示而不是静默无反应。
                    shopNotice = "Empty slot: nothing to buy";
                }
                else if(resourceCount < item.price){
                    shopNotice = "Not enough resources: need " + to_string(item.price);
                }
                else{
                    //扣款和加库存必须一起成功,所以前面两种情况直接返回。
                    resourceCount -= item.price;
                    item.owned++;
                    shopNotice = string("Bought ") + item.name + " for " + to_string(item.price);
                }
                shopChanged = true;
            }
        }
        if(canPlay && scene == 0){
            if(remainResource <= 50){
                for(int row = 0; row < MAP_H; row++){
                    for(int column = 0; column < MAP_W; column++){
                        if(!resource[row][column] && rand() % 100 < RESOURCE_RATE){
                            resource[row][column] = true;
                            remainResource++;
                        }
                    }
                }
            }
        }
        //少了 shopChanged,购买和移动光标都不会触发重绘:画面只认这里列出的状态变化。
        bool changed = resized || scene != oldScene || x != oldX || y != oldY ||
                       resourceCount != oldResourceCount || remainResource != oldRemainResource ||
                       shopChanged;
        if(changed){
            fill(frame.begin(), frame.end(), blank);
            needsPresent = true;
            if(!canPlay){
                writeText(0, 0, "Resize terminal: minimum 32 x 12", width);
                writeText(0, 1, "Q Quit", width);
            }
            else if(scene == 0){
                bool sidebar = width >= 80;
                int viewW = min(MAP_W, width - (sidebar ? 31 : 0));
                int viewH = min(MAP_H, height - (sidebar ? 0 : 6));
                int cameraX = clamp(x - viewW / 2, 0, MAP_W - viewW);
                int cameraY = clamp(y - viewH / 2, 0, MAP_H - viewH);
                for(int row = 0; row < viewH; row++){
                    for(int column = 0; column < viewW; column++){
                        int mapX = cameraX + column;
                        int mapY = cameraY + row;
                        char value = mapX == x && mapY == y ? '@' :
                                     (resource[mapY][mapX] ? '#' : ' ');
                        setCell(column, row, value);
                    }
                }
                string position = "X:" + to_string(x) + " Y:" + to_string(y);
                if(sidebar){
                    for(int row = 0; row < height; row++)
                        setCell(viewW, row, '|');
                    int column = viewW + 1;
                    int infoWidth = width - column;
                    writeText(column, 0, "PYS", infoWidth);
                    writeText(column, 2, position, infoWidth);
                    writeText(column, 3, "Collected:" + to_string(resourceCount), infoWidth);
                    writeText(column, 4, "Remaining:" + to_string(remainResource), infoWidth);
                    writeText(column, 6, "Map:128 x 32", infoWidth);
                    writeText(column, 7, "View:" + to_string(viewW) + " x " + to_string(viewH), infoWidth);
                    writeText(column, 9, "WASD Move  E Collect", infoWidth);
                    writeText(column, 10, "Right Shop  Q Quit", infoWidth);
                }
                else{
                    for(int column = 0; column < width; column++)
                        setCell(column, viewH, '-');
                    writeText(0, viewH + 1, position, width);
                    writeText(0, viewH + 2, "Collected:" + to_string(resourceCount), width);
                    writeText(0, viewH + 3, "Remaining:" + to_string(remainResource), width);
                    writeText(0, viewH + 4, "WASD Move  E Collect", width);
                    writeText(0, viewH + 5, "Right Shop  Q Quit", width);
                }
            }
            else{
                //商店画面完全由 shopItems 驱动:商品表加减行,这里跟着变,不需要改代码。
                int shopWidth = min(48, width - 2);
                int itemCount = static_cast<int>(shopItems.size());
                int panelHeight = itemCount + 8;   //标题 + 列表 + 余额 + 反馈 + 两行提示占的总行数
                int column = (width - shopWidth) / 2;
                int row = max(0, (height - panelHeight) / 2);
                writeText(column, row, "============ SHOP ============", shopWidth);
                //光标所在行以 > 开头;实装商品显示 价格 和 已有数量,空位显示占位文字。
                for(int index = 0; index < itemCount; index++){
                    const ShopItem& item = shopItems[index];
                    string line = (index == shopSelection ? "> " : "  ") + to_string(index + 1) + ". ";
                    if(item.available)
                        line += string(item.name) + "  " + to_string(item.price) + "  x" + to_string(item.owned);
                    else
                        line += "(empty slot)";
                    writeText(column, row + 2 + index, line, shopWidth);
                }
                writeText(column, row + itemCount + 2, "Collected:" + to_string(resourceCount), shopWidth);
                writeText(column, row + itemCount + 3, shopNotice, shopWidth);
                writeText(column, row + itemCount + 5, "Up/Down Select   Enter Buy", shopWidth);
                writeText(column, row + itemCount + 6, "Left Game   Q Quit", shopWidth);
            }
        }
        present();
        Sleep(30);
    }
    //两种退出方式共用此路径,只清除当前可见区域,保留终端滚动历史。
    CONSOLE_SCREEN_BUFFER_INFO exitInfo;
    if(GetConsoleScreenBufferInfo(hConsole, &exitInfo)){
        COORD clearSize = {
            static_cast<SHORT>(exitInfo.srWindow.Right - exitInfo.srWindow.Left + 1),
            static_cast<SHORT>(exitInfo.srWindow.Bottom - exitInfo.srWindow.Top + 1)
        };
        vector<CHAR_INFO> clearFrame(static_cast<size_t>(clearSize.X) * clearSize.Y, blank);
        SMALL_RECT target = exitInfo.srWindow;
        WriteConsoleOutputA(hConsole, clearFrame.data(), clearSize, COORD{0, 0}, &target);
        SetConsoleCursorPosition(hConsole, COORD{exitInfo.srWindow.Left, exitInfo.srWindow.Top});
    }
    if(restoreCursor)
        SetConsoleCursorInfo(hConsole, &originalCursor);
    FlushConsoleInputBuffer(hInput);
    SetConsoleMode(hInput, originalInputMode);
    SetConsoleCtrlHandler(HandleConsoleControl, FALSE);
    return exitCode;
}
