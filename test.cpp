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
    int cap;   //最多能买几份,0 表示不限。机器人用它锁死 32 行,升级用它锁死五档。
};

//一个正在地图上跑动的机器人。dir 是移动方向(+1 向右 / -1 向左),
//justAte 记录上一次迈步有没有吃到资源,决定这一格画 = 还是 < / >。
struct Bot{
    int x;
    int y;
    int dir;
    bool justAte;
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
    //资源密度百分比。debug 模式下可以实时改,改完整张图立刻按新密度重铺。
    int resourceRate = 6;
    bool resource[MAP_H][MAP_W] = {};
    int remainResource = 0;
    srand(static_cast<unsigned int>(time(nullptr)));
    //按当前密度重铺整张地图并重算剩余量。开局铺图和 debug 调密度共用这一份,避免两处写法走偏。
    auto layoutResource = [&](){
        remainResource = 0;
        for(int row = 0; row < MAP_H; row++){
            for(int column = 0; column < MAP_W; column++){
                resource[row][column] = (rand() % 100 < resourceRate);
                if(resource[row][column])
                    remainResource++;
            }
        }
    };
    layoutResource();

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
    const int SHOP_ROBOT = 0;      //商品表里只有这一行会生成跑动单位
    const int SHOP_SPEEDUP = 1;    //这一行不改数量上限,而是给全体机器人提速
    const int SPEED_MAX = 4;       //Bot Speedup 最高买到第 4 档
    //五档步长(毫秒),下标就是已购档数:第 0 档是初始的 1.0s 一格,买到第 4 档变 0.1s。
    const int BOT_SPEED_MS[SPEED_MAX + 1] = {1000, 750, 500, 250, 100};
    //商品表驱动商店的画面和购买:加新商品只需在这里多填一行,选择、渲染、扣费、上限都跟着走。
    //后面两项是留出来的空位(available=false),给后续 commit 填实际商品。
    vector<ShopItem> shopItems = {
        {"Auto Robot", 100, 0, true, MAP_H},      //一行只能站一个,32 行买满就锁死
        {"Bot Speedup", 1000, 0, true, SPEED_MAX},//五档升级,买满第 4 档到顶
        {"", 0, 0, false, 0},
        {"", 0, 0, false, 0},
    };
    int shopSelection = 0;                          //商店光标指向的商品下标
    string shopNotice = "Pick an item, Enter to buy";  //商店底部反馈行
    vector<Bot> bots;               //买一个多一个,主循环里按 owned 补齐
    unsigned long long botDeadline = GetTickCount64() + BOT_SPEED_MS[0];
    //调试模式的开门短语。存大写,因为字母键的 wVirtualKeyCode 本身就是大写码,与 Shift 无关。
    const string DEBUG_PHRASE = "OWOISHERE";
    string debugBuffer;             //最近按下的几个字母,只留够匹配短语的长度
    bool debugOn = false;
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
        //机器人迈步也算状态变化,同样要上报给下面的 changed,否则画面不动。
        bool botActed = false;
        //调试操作(开启、发钱、改密度)同样要自己上报,不然侧栏不会刷新。
        bool debugActed = false;
        for(const auto& key : keys){
            int code = key.wVirtualKeyCode;
            int repeat = max<int>(1, key.wRepeatCount);
            if(code == VK_RIGHT) scene = 1;
            if(code == VK_LEFT) scene = 0;
            if(!canPlay)
                continue;
            //调试短语逐键累积,采集和商店场景都能敲。只保留最后 9 个字母,
            //所以中间打错不用整串重来,接着往下敲就行。
            if(code >= 'A' && code <= 'Z'){
                debugBuffer += static_cast<char>(code);
                if(debugBuffer.size() > DEBUG_PHRASE.size())
                    debugBuffer.erase(0, debugBuffer.size() - DEBUG_PHRASE.size());
                if(debugBuffer == DEBUG_PHRASE){
                    //同一个短语兼作开关:再打一遍就关掉。已发的钱和调过的密度不回滚。
                    debugOn = !debugOn;
                    debugBuffer.clear();
                    debugActed = true;
                }
            }
            //调试快捷键只在开启之后才响应。这几个字母本身也在短语里,
            //所以它们会先进缓冲、再被当成命令执行,两者不冲突。
            if(debugOn){
                if(code == 'F'){
                    //直接记账,不动 Remaining:发钱不该改变地图上 # 的数量。
                    resourceCount += 10000;
                    debugActed = true;
                }
                if(code == 'T'){
                    //密度调完立刻重铺整张图,否则要等资源自己见底才看得出效果。
                    resourceRate = min(100, resourceRate + 1);
                    layoutResource();
                    debugActed = true;
                }
                if(code == 'Y'){
                    resourceRate = max(0, resourceRate - 1);
                    layoutResource();
                    debugActed = true;
                }
            }
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
                else if(item.cap > 0 && item.owned >= item.cap){
                    //到顶就不再扣钱:机器人占满 32 行、升级买满第 4 档都走这里挡掉。
                    shopNotice = string(item.name) + " is maxed out";
                }
                else if(resourceCount < item.price){
                    shopNotice = "Not enough #: need " + to_string(item.price);
                }
                else{
                    //扣款和加库存必须一起成功,所以前面三种情况都在上面被拦掉了。
                    int paid = item.price;   //先记下成交价,Bot Speedup 会把挂牌价改掉
                    resourceCount -= paid;
                    item.owned++;
                    //递档升级:买完一档把挂牌价翻倍,对应 1000 / 2000 / 4000 / 8000。
                    if(shopSelection == SHOP_SPEEDUP && item.owned < SPEED_MAX)
                        item.price *= 2;
                    shopNotice = string("Bought ") + item.name + " for " + to_string(paid) + "#";
                }
                shopChanged = true;
            }
        }
        //机器人只在采集场景跑;切到商店时冻住,不会在看不见的地方偷偷吃资源。
        if(canPlay && scene == 0){
            //按已购数量补齐单位,买一个就多一个。
            while(static_cast<int>(bots.size()) < shopItems[SHOP_ROBOT].owned){
                int index = static_cast<int>(bots.size());
                Bot fresh;
                fresh.x = 1;
                //一行只站一个,从 y=0 的第一行开始依次往下排;32 行排满后 cap 就不让再买了。
                fresh.y = index % MAP_H;
                fresh.dir = 1;
                fresh.justAte = false;
                bots.push_back(fresh);
            }
            unsigned long long now = GetTickCount64();
            if(now >= botDeadline){
                //到点才迈步。下一次按当前时间重新排,卡顿掉的份额不会追补成一串脚步。
                //步长取当前升级档位,买到 Bot Speedup 后下一脚就按新间隔走。
                botDeadline = now + BOT_SPEED_MS[shopItems[SHOP_SPEEDUP].owned];
                botActed = true;
                for(Bot& bot : bots){
                    //先走到下一格,再看这一格有没有资源:吃到才亮出咬合的 =,否则保持张口的 < 或 >。
                    bot.x += bot.dir;
                    if(bot.x >= MAP_W - 1){
                        //撞到右侧墙壁,换成朝左的 > 往回走。
                        bot.x = MAP_W - 1;
                        bot.dir = -1;
                    }
                    else if(bot.x <= 0){
                        //左侧墙壁同理,换回 < 向右,如此往复。
                        bot.x = 0;
                        bot.dir = 1;
                    }
                    bot.justAte = false;
                    if(resource[bot.y][bot.x]){
                        //机器人采到的资源直接进背包,和玩家亲手采的记在同一笔账上。
                        resource[bot.y][bot.x] = false;
                        remainResource--;
                        resourceCount++;
                        bot.justAte = true;
                    }
                }
            }
        }
        if(canPlay && scene == 0){
            if(remainResource <= 50){
                for(int row = 0; row < MAP_H; row++){
                    for(int column = 0; column < MAP_W; column++){
                        if(!resource[row][column] && rand() % 100 < resourceRate){
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
                       shopChanged || botActed || debugActed;
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
                //机器人盖在资源之上、玩家之下:它踩到的 # 被符号顶掉,但玩家 @ 优先,重叠时不画机器人。
                for(const Bot& bot : bots){
                    if(bot.x == x && bot.y == y)
                        continue;
                    int screenX = bot.x - cameraX;
                    int screenY = bot.y - cameraY;
                    //地图比可见区域大,跑出视口的机器人不画。
                    if(screenX < 0 || screenX >= viewW || screenY < 0 || screenY >= viewH)
                        continue;
                    //吃到就是咬合的 =,没吃到按方向显示张口的 < 或 >。
                    setCell(screenX, screenY, bot.justAte ? '=' : (bot.dir > 0 ? '<' : '>'));
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
                    writeText(column, 9, "WASD Move  E/Space Collect", infoWidth);
                    writeText(column, 10, "Right Shop  Q Quit", infoWidth);
                    //正在运作的东西标在侧栏。只有 Auto Robot 会产生跑动单位,
                    //Bot Speedup 是全体提速,不单独占一行,而是体现在下面那个步长上。
                    //这几行排在第 12 行之后,终端高度刚好卡在最小值 12 时会画到画面外。
                    int listRow = 12;
                    writeText(column, listRow, "Running:", infoWidth);
                    listRow++;
                    writeText(column, listRow, "Bots:" + to_string(bots.size()) + "/" + to_string(MAP_H), infoWidth);
                    listRow++;
                    //步长按已购档位取,格式化成两位小数:1000ms 显示 1.00s,100ms 显示 0.10s。
                    char speedText[16];
                    snprintf(speedText, sizeof(speedText), "%.2fs",
                             BOT_SPEED_MS[shopItems[SHOP_SPEEDUP].owned] / 1000.0);
                    writeText(column, listRow,
                              string("Speed:Lv") + to_string(shopItems[SHOP_SPEEDUP].owned) + " " +
                              speedText + "/step", infoWidth);
                    //调试状态标在侧栏,可用键也一并写出来,免得回头翻代码。
                    if(debugOn){
                        listRow++;
                        writeText(column, listRow, "DEBUG ON", infoWidth);
                        listRow++;
                        writeText(column, listRow, "# density:" + to_string(resourceRate) + "%", infoWidth);
                        listRow++;
                        writeText(column, listRow, "F +10000#  T up  Y down", infoWidth);
                        listRow++;
                        writeText(column, listRow, "owoishere again: off", infoWidth);
                    }
                }
                else{
                    for(int column = 0; column < width; column++)
                        setCell(column, viewH, '-');
                    writeText(0, viewH + 1, position, width);
                    writeText(0, viewH + 2, "Collected:" + to_string(resourceCount), width);
                    writeText(0, viewH + 3, "Remaining:" + to_string(remainResource), width);
                    writeText(0, viewH + 4, "WASD Move  E/Space Collect", width);
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
                    if(item.available){
                        //买满的货把价格换成 MAX:留着挂牌价会让人以为还能再买一次。
                        //其余情况 # 是资源计价单位,数量写成 已购/上限。
                        line += string(item.name) + "  ";
                        if(item.cap > 0 && item.owned >= item.cap)
                            line += "MAX";
                        else
                            line += to_string(item.price) + "#";
                        line += "  x" + to_string(item.owned);
                        if(item.cap > 0)
                            line += "/" + to_string(item.cap);
                    }
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
