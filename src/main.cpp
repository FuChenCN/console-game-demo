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

#include "platform/console_ctrl.h"   //退出标志与 Ctrl+C/Ctrl+Break 回调
#include "game/world.h"              //资源网格、密度与机器人的推进规则
#include "content/title_assets.h"    //标题页的字模、色板与节奏

using namespace std;

//制作配方:成本按矿种分开;cap=0 表示不限量,owned 指向背包里的成品计数。
struct Recipe{
    const char* name;
    int costIron;
    int costCopper;
    int costTin;
    int cap;
    int* owned;
};

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

    //世界状态已收进 game/world.h:网格、密度、剩余量、机器人。
    World world;
    //种子用于重建初始分布并显示在调试面板;存档还要记录运行期变化和随机引擎状态。
    //随机源不再走全局 rand()/srand()。
    uint32_t worldSeed = static_cast<uint32_t>(time(nullptr));
    //尺寸同样是运行期参数:默认 512x512,以后"世界规模选择"界面把玩家选的宽高传进这里。
    world.init(DEFAULT_MAP_W, DEFAULT_MAP_H, 6, worldSeed);

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
    //第四个参数是控制台颜色属性,传 -1 表示"不改这个格子的颜色"。
    //lambda 的默认参数不能引用捕获的局部变量,所以这里用 -1 当哨兵;
    //配合 changed 分支里的 fill(frame, blank),不传颜色就等于用终端默认色。
    auto setCell = [&](int column, int row, char value, int attributes = -1){
        if(column >= 0 && column < bufferSize.X && row >= 0 && row < bufferSize.Y){
            int index = row * bufferSize.X + column;
            frame[index].Char.AsciiChar = value;
            if(attributes >= 0)
                frame[index].Attributes = static_cast<BYTE>(attributes);
        }
    };
    auto writeText = [&](int column, int row, const string& text, int width, int attributes = -1){
        int count = min(width, static_cast<int>(text.size()));
        for(int offset = 0; offset < count; offset++)
            setCell(column + offset, row, text[offset], attributes);
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
                    //颜色也要比:只比字符的话,字不变、只变色的动画帧会被当成"没变化"而跳过提交。
                    if(frame[index].Char.AsciiChar == displayed[index].Char.AsciiChar &&
                       frame[index].Attributes == displayed[index].Attributes)
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

    //玩家出生在世界中心。原先写死的 10,10 是按 128x32 那张图定的,放到 512x512 就成了贴左上角。
    int x = world.width / 2;
    int y = world.height / 2;
    //场景:0 采集,1 背包+制作,2 标题。开局停在标题,按 Enter 才进采集页。
    int scene = 2;
    int titlePhase = 0;                                        //当前颜色相位
    unsigned long long titleDeadline = GetTickCount64();       //下次推相位的时刻
    const int SPEED_MAX = 4;       //Rig Speedup 最高做到第 4 档
    //五档步长(毫秒),下标就是已购档数:第 0 档是初始的 1.0s 一格,买到第 4 档变 0.1s。
    const int RIG_SPEED_MS[SPEED_MAX + 1] = {1000, 750, 500, 250, 100};
    //三类矿脉各有一套颜色:满格用亮色,挖过但没挖空的格子换成同色系的暗色。
    //控制台只有 16 色,铜的"橙"用亮黄近似,锡的"青灰"用亮青近似。
    const BYTE ORE_COLORS[ORE_KIND_COUNT] = {
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,        //铁:浅灰
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY,   //铜:橙
        FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY,  //锡:青灰
    };
    const BYTE ORE_COLORS_DIM[ORE_KIND_COUNT] = {
        FOREGROUND_INTENSITY,                                       //铁:深灰
        FOREGROUND_RED | FOREGROUND_GREEN,                          //铜:暗黄
        FOREGROUND_GREEN | FOREGROUND_BLUE,                         //锡:暗青
    };
    //每个矿格在屏幕上占两列,直接写元素符号。
    const char* ORE_LABELS[ORE_KIND_COUNT] = {"Fe", "Cu", "Sn"};
    const BYTE PLAYER_ATTR = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    const BYTE RIG_ATTR = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    //背包按矿种分开计数;制作扣对应矿种。
    int invIron = 0;
    int invCopper = 0;
    int invTin = 0;
    int rigsOwned = 0;          //已制作总数,包含已经放出去的
    int speedLevel = 0;         //0..SPEED_MAX
    //制作表驱动右栏:加一个配方只需多填一行,成本、上限、数量都跟着走。
    vector<Recipe> recipes = {
        {"Mining Rig", 60, 30, 10, 0, &rigsOwned},          //不限量,做多少都能备着
        {"Rig Speedup", 200, 100, 100, SPEED_MAX, &speedLevel},
    };
    int recipeSelection = 0;                        //制作面板光标
    string craftNotice = "Up/Down select, Enter craft";
    string playNotice;                              //采集场景的放置反馈(2.5 秒后自动消失)
    unsigned long long playNoticeUntil = 0;
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
        int oldRemainResource = world.remainResource;
        //矿机取矿、矿池回填也算状态变化,同样要上报给下面的 changed,否则画面不动。
        bool poolActed = false;
        //放置矿机、提示文字出现/消失同样自己上报。
        bool uiActed = false;
        //调试操作(开启、发钱、改密度)同样要自己上报,不然侧栏不会刷新。
        bool debugActed = false;
        for(const auto& key : keys){
            int code = key.wVirtualKeyCode;
            int repeat = max<int>(1, key.wRepeatCount);
            //标题页不响应左右切换,否则一进标题就被方向键带去背包场景。
            if(scene != 2){
                if(code == VK_RIGHT) scene = 1;
                if(code == VK_LEFT) scene = 0;
            }
            if(!canPlay)
                continue;
            //调试短语逐键累积,采集和背包场景都能敲。只保留最后 9 个字母,
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
                    //调试补给:三类矿各加 10000,不动 Remaining(地图上的 # 不变)。
                    invIron += 10000;
                    invCopper += 10000;
                    invTin += 10000;
                    debugActed = true;
                }
                if(code == 'T'){
                    //密度调完立刻重铺整张图,否则要等资源自己见底才看得出效果。
                    world.resourceRate = min(100, world.resourceRate + 1);
                    world.relayout();
                    debugActed = true;
                }
                if(code == 'Y'){
                    world.resourceRate = max(0, world.resourceRate - 1);
                    world.relayout();
                    debugActed = true;
                }
            }
            //标题页只认 Enter 开始采集,其余按键忽略(退出仍由前面 Q 的判断处理,调试键在上面那段处理)。
            if(scene == 2){
                if(code == VK_RETURN)
                    scene = 0;   //scene 一变,下面的 changed 就会触发重绘
                continue;
            }
            if(scene == 0){
                //采集场景:WASD 移动,E 或空格采掉脚下的资源,Enter 放一台矿机。
                if(code == 'W') y = max(0, y - repeat);
                if(code == 'S') y = min(world.height - 1, y + repeat);
                if(code == 'A') x = max(0, x - repeat);
                if(code == 'D') x = min(world.width - 1, x + repeat);
                if(code == 'E' || code == VK_SPACE){
                    OreKind kind;
                    if(world.oreKindAt(x, y, kind) && world.collectAt(x, y)){
                        //按矿种入包:铁/铜/锡分开记。
                        switch(kind){
                        case OreKind::Iron: invIron++; break;
                        case OreKind::Copper: invCopper++; break;
                        case OreKind::Tin: invTin++; break;
                        }
                    }
                }
                if(code == VK_RETURN){
                    const unsigned long long now = GetTickCount64();
                    const int owned = rigsOwned;
                    const int placed = static_cast<int>(world.rigs.size());
                    const int stepMs = RIG_SPEED_MS[speedLevel];
                    if(placed >= owned){
                        playNotice = "No rig: craft one in the bag";
                    }
                    else if(world.placeRig(x, y, now, stepMs)){
                        //放置不结算资源:矿机占住这一格,落地后自己从脚下开始挖。
                        playNotice = "Rig placed, spare:" + to_string(owned - placed - 1);
                    }
                    else{
                        playNotice = "Stand on # to place a rig";
                    }
                    playNoticeUntil = now + 2500;
                    uiActed = true;
                }
                continue;
            }
            //背包场景:右栏是制作面板。W/S(或上下)选配方,Enter 制作。
            const int recipeCount = static_cast<int>(recipes.size());
            if(code == 'W' || code == VK_UP){
                recipeSelection = (recipeSelection + recipeCount - 1) % recipeCount;
                uiActed = true;
            }
            if(code == 'S' || code == VK_DOWN){
                recipeSelection = (recipeSelection + 1) % recipeCount;
                uiActed = true;
            }
            if(code == VK_RETURN){
                Recipe& recipe = recipes[recipeSelection];
                if(recipe.cap > 0 && *recipe.owned >= recipe.cap){
                    craftNotice = string(recipe.name) + " is maxed out";
                }
                else if(invIron < recipe.costIron || invCopper < recipe.costCopper ||
                        invTin < recipe.costTin){
                    craftNotice = "Not enough ore";
                }
                else{
                    invIron -= recipe.costIron;
                    invCopper -= recipe.costCopper;
                    invTin -= recipe.costTin;
                    (*recipe.owned)++;
                    //递档升级:每做一档把三样成本都翻倍,直到买满。
                    if(recipe.owned == &speedLevel && *recipe.owned < SPEED_MAX){
                        recipe.costIron *= 2;
                        recipe.costCopper *= 2;
                        recipe.costTin *= 2;
                    }
                    craftNotice = string("Crafted ") + recipe.name;
                }
                uiActed = true;
            }
        }
        //矿池和矿机只在采集场景运转;切到背包时冻住,不会在看不见的地方偷偷产出。
        if(canPlay && scene == 0){
            const unsigned long long now = GetTickCount64();
            //先按 5 分钟节奏给未满的池补一格,再让到点的矿机出手。
            const int grown = world.updatePools(now);
            const OreYield mined = world.advanceRigs(now, RIG_SPEED_MS[speedLevel]);
            invIron += mined.iron;
            invCopper += mined.copper;
            invTin += mined.tin;
            poolActed = (grown > 0) || (mined.total() > 0);
            if(!playNotice.empty() && now >= playNoticeUntil){
                playNotice.clear();
                uiActed = true;
            }
        }
        //标题页每 TITLE_STEPS_MS 推一次颜色相位;不在标题页就不推,免得白耗重绘。
        bool titleAnimated = false;
        if(canPlay && scene == 2){
            unsigned long long titleNow = GetTickCount64();
            if(titleNow >= titleDeadline){
                titleDeadline = titleNow + TITLE_STEPS_MS;
                titlePhase = (titlePhase + 1) % TITLE_COLOR_COUNT;
                titleAnimated = true;
            }
        }
        //制作、选配方、放置和提示变化都通过 uiActed 上报;采集与回填靠 remainResource 比较。
        bool changed = resized || scene != oldScene || x != oldX || y != oldY ||
                       world.remainResource != oldRemainResource ||
                       poolActed || uiActed || debugActed || titleAnimated;
        if(changed){
            fill(frame.begin(), frame.end(), blank);
            needsPresent = true;
            if(!canPlay){
                writeText(0, 0, "Resize terminal: minimum 32 x 12", width);
                writeText(0, 1, "Q Quit", width);
            }
            else if(scene == 2){
                //标题页。同一个色相分两层画:本体是亮色密实的 '#',阴影是暗色稀疏的 '.',各向右下偏一格。
                //密度差叠加亮度差双重拉开,才不会和字体糊成一块;颜色相位按字母错开,整串字横向流动变色。
                const int letterW = 4;
                const int letterH = 5;
                const int gap = 2;   //留两列:阴影会右偏一列,单列间隔会被暗色填满,字母看着粘连
                const int wordW = 7 * (letterW + gap) - gap;   //CONSOLE 占 40 列
                int startX = (width - (wordW + 1)) / 2;         //多留一列给阴影
                int startY = max(1, (height - letterH) / 2 - 2);
                if(width >= wordW + 6){
                    for(int pass = 0; pass < 2; pass++){
                        //pass 0 画暗色阴影(右下各偏一格),pass 1 画亮色本体。
                        //必须分两趟,交错着画会让后画的阴影盖掉前一个字母的主体。
                        bool shadow = (pass == 0);
                        int offset = shadow ? 1 : 0;
                        for(int letter = 0; letter < 7; letter++){
                            BYTE hue = TITLE_COLORS[(letter + titlePhase) % TITLE_COLOR_COUNT];
                            BYTE attr = shadow ? hue : static_cast<BYTE>(hue | FOREGROUND_INTENSITY);
                            const string* glyph = TITLE_GLYPHS[TITLE_LETTERS[letter]];
                            for(int row = 0; row < letterH; row++){
                                for(int col = 0; col < letterW; col++){
                                    if(glyph[row][col] != '#')
                                        continue;
                                    //阴影画成稀疏的 '.',主体是密实的 '#':控制台没有 alpha 通道,
                                    //字符密度差就是能拿到的"半透明"。
                                    setCell(startX + letter * (letterW + gap) + col + offset,
                                            startY + row + offset, shadow ? '.' : '#', attr);
                                }
                            }
                        }
                    }
                }
                else{
                    //窗口放不下 40 列大字,退化成一行带空格的普通字,标题至少还在。
                    writeText(max(0, (width - 13) / 2), startY, "C O N S O L E", width,
                              static_cast<BYTE>(TITLE_COLORS[titlePhase % TITLE_COLOR_COUNT] | FOREGROUND_INTENSITY));
                }
                int hintRow = startY + letterH + 2;
                writeText(max(0, (width - 21) / 2), hintRow, "Press ENTER to start", width);
                writeText(max(0, (width - 8) / 2), hintRow + 1, "PYS demo", width);
                writeText(max(0, (width - 6) / 2), hintRow + 2, "Q Quit", width);
            }
            else if(scene == 0){
                bool sidebar = width >= 80;
                //注意两套尺寸:width / height 是终端窗口大小,world.width / world.height 是地图大小。
                //每个地图格占 2 个屏幕列(Fe/Cu/Sn 两个字),所以横向能看到的格子数只有列数的一半。
                int available = width - (sidebar ? 31 : 0);
                int viewW = min(world.width, available / 2);
                int viewH = min(world.height, height - (sidebar ? 0 : 6));
                int cameraX = clamp(x - viewW / 2, 0, world.width - viewW);
                int cameraY = clamp(y - viewH / 2, 0, world.height - viewH);
                for(int row = 0; row < viewH; row++){
                    for(int cell = 0; cell < viewW; cell++){
                        const int screenX = cell * 2;
                        const int mapX = cameraX + cell;
                        const int mapY = cameraY + row;
                        if(mapX == x && mapY == y){
                            setCell(screenX, row, '@', PLAYER_ATTR);
                            setCell(screenX + 1, row, ' ', PLAYER_ATTR);
                            continue;
                        }
                        const uint16_t poolId = world.poolAt(mapX, mapY);
                        if(poolId != 0){
                            //颜色跟着矿脉类型走:铁灰、铜橙、锡青灰。
                            //挖过但没挖空的格子换成同色系的暗色,一眼能看出这格被啃过。
                            const int kind = static_cast<int>(world.pools[poolId - 1].kind);
                            const BYTE color = world.oreLeftAt(mapX, mapY) == ORE_PER_CELL
                                                   ? ORE_COLORS[kind]
                                                   : ORE_COLORS_DIM[kind];
                            const char* label = ORE_LABELS[kind];
                            setCell(screenX, row, label[0], color);
                            setCell(screenX + 1, row, label[1], color);
                        }
                        else{
                            setCell(screenX, row, ' ');
                            setCell(screenX + 1, row, ' ');
                        }
                    }
                }
                //矿机盖在矿格之上、玩家之下:玩家 @ 优先,重叠时不画矿机。
                for(const Rig& rig : world.rigs){
                    if(rig.x == x && rig.y == y)
                        continue;
                    //跑出视口的矿机不画。
                    if(rig.x < cameraX || rig.x >= cameraX + viewW ||
                       rig.y < cameraY || rig.y >= cameraY + viewH)
                        continue;
                    const int screenX = (rig.x - cameraX) * 2;
                    const int screenY = rig.y - cameraY;
                    setCell(screenX, screenY, '&', RIG_ATTR);
                    setCell(screenX + 1, screenY, ' ', RIG_ATTR);
                }
                string position = "X:" + to_string(x) + " Y:" + to_string(y);
                if(sidebar){
                    for(int row = 0; row < height; row++)
                        setCell(viewW * 2, row, '|');
                    int column = viewW * 2 + 1;
                    int infoWidth = width - column;
                    writeText(column, 0, "PYS", infoWidth);
                    writeText(column, 2, position, infoWidth);
                    writeText(column, 3,
                              "Fe:" + to_string(invIron) + " Cu:" + to_string(invCopper) +
                              " Sn:" + to_string(invTin), infoWidth);
                    writeText(column, 4, "Remaining:" + to_string(world.remainResource), infoWidth);
                    writeText(column, 6, "Map:" + to_string(world.width) + " x " + to_string(world.height), infoWidth);
                    writeText(column, 7, "View:" + to_string(viewW) + " x " + to_string(viewH), infoWidth);
                    writeText(column, 9, "WASD Move  E/Space Collect", infoWidth);
                    writeText(column, 10, "Right Bag  Enter Rig  Q Quit", infoWidth);
                    //第 11 行留给放置反馈和备用矿机数量,不占用下面的 Running 段。
                    if(!playNotice.empty())
                        writeText(column, 11, playNotice, infoWidth);
                    else
                        writeText(column, 11,
                                  "Rigs spare:" + to_string(rigsOwned -
                                                            static_cast<int>(world.rigs.size())),
                                  infoWidth);
                    //正在运作的东西标在侧栏:矿机数量、当前速度档。
                    //Rig Speedup 不给矿机单独占行,而是体现在下面那个步长上。
                    //这几行排在第 12 行之后,终端高度刚好卡在最小值 12 时会画到画面外。
                    int listRow = 12;
                    writeText(column, listRow, "Running:", infoWidth);
                    listRow++;
                    writeText(column, listRow,
                              "Rigs:" + to_string(world.rigs.size()) + "/" +
                              to_string(rigsOwned), infoWidth);
                    listRow++;
                    //步长按已购档位取,格式化成两位小数:1000ms 显示 1.00s,100ms 显示 0.10s。
                    char speedText[16];
                    snprintf(speedText, sizeof(speedText), "%.2fs",
                             RIG_SPEED_MS[speedLevel] / 1000.0);
                    writeText(column, listRow,
                              string("Speed:Lv") + to_string(speedLevel) + " " +
                              speedText + "/step", infoWidth);
                    //调试状态标在侧栏,可用键也一并写出来,免得回头翻代码。
                    if(debugOn){
                        listRow++;
                        writeText(column, listRow, "DEBUG ON", infoWidth);
                        listRow++;
                        writeText(column, listRow, "# density:" + to_string(world.resourceRate) + "%", infoWidth);
                        listRow++;
                        //种子显示出来才谈得上复现:嫌图太密/太稀时,把这个数报过来就够,不用截图。
                        writeText(column, listRow, "seed:" + to_string(world.seed), infoWidth);
                        listRow++;
                        int kindCount[ORE_KIND_COUNT] = {0, 0, 0};
                        for(const OrePool& pool : world.pools)
                            kindCount[static_cast<int>(pool.kind)]++;
                        writeText(column, listRow,
                                  "pools:" + to_string(world.pools.size()) +
                                  " Fe/Cu/Sn:" + to_string(kindCount[0]) + "/" +
                                  to_string(kindCount[1]) + "/" + to_string(kindCount[2]) +
                                  " rigs:" + to_string(world.rigs.size()), infoWidth);
                        listRow++;
                        writeText(column, listRow, "regrow:+1#/5min  cell:100#", infoWidth);
                        listRow++;
                        writeText(column, listRow, "F +10k each  T up  Y down", infoWidth);
                        listRow++;
                        writeText(column, listRow, "owoishere again: off", infoWidth);
                    }
                }
                else{
                    for(int column = 0; column < width; column++)
                        setCell(column, viewH, '-');
                    writeText(0, viewH + 1, position, width);
                    writeText(0, viewH + 2,
                              "Fe:" + to_string(invIron) + " Cu:" + to_string(invCopper) +
                              " Sn:" + to_string(invTin), width);
                    writeText(0, viewH + 3, "Remaining:" + to_string(world.remainResource), width);
                    writeText(0, viewH + 4,
                              playNotice.empty() ? string("WASD Move  E/Space Collect") : playNotice,
                              width);
                    writeText(0, viewH + 5, "Right Bag  Enter Rig  Q Quit", width);
                }
            }
            else{
                //背包 + 制作:宽窗口左右两栏,窄窗口上下堆叠。
                //左栏是只读库存,右栏选配方、Enter 制作;配方表加一行就多一个可选配方。
                const bool split = width >= 64;
                const int bagWidth = split ? 29 : width;
                const int craftColumn = split ? bagWidth + 1 : 0;
                const int craftWidth = split ? width - craftColumn : width;
                char speedText[16];
                snprintf(speedText, sizeof(speedText), "%.2fs", RIG_SPEED_MS[speedLevel] / 1000.0);

                if(split){
                    writeText(0, 0, "========== BAG ==========", bagWidth);
                    writeText(0, 2, "Iron   :" + to_string(invIron), bagWidth);
                    writeText(0, 3, "Copper :" + to_string(invCopper), bagWidth);
                    writeText(0, 4, "Tin    :" + to_string(invTin), bagWidth);
                    writeText(0, 5, "Total  :" + to_string(invIron + invCopper + invTin), bagWidth);
                    writeText(0, 6, "Rigs   :" + to_string(world.rigs.size()) + "/" + to_string(rigsOwned), bagWidth);
                    writeText(0, 7, "Speed  :Lv" + to_string(speedLevel) + " " + speedText, bagWidth);
                    writeText(0, 8, "Spare  :" + to_string(rigsOwned - static_cast<int>(world.rigs.size())), bagWidth);
                    for(int row = 0; row < height; row++)
                        setCell(bagWidth, row, '|');
                }
                else{
                    writeText(0, 0, "========== BAG ==========", width);
                    writeText(0, 1, "Iron   :" + to_string(invIron), width);
                    writeText(0, 2, "Copper :" + to_string(invCopper), width);
                    writeText(0, 3, "Tin    :" + to_string(invTin), width);
                    writeText(0, 4, "Total  :" + to_string(invIron + invCopper + invTin), width);
                    writeText(0, 5, "Rigs   :" + to_string(world.rigs.size()) + "/" + to_string(rigsOwned), width);
                    writeText(0, 6, "Speed  :Lv" + to_string(speedLevel) + " " + speedText, width);
                }

                const int base = split ? 0 : 7;
                writeText(craftColumn, base, "========== CRAFT ==========", craftWidth);
                for(int index = 0; index < static_cast<int>(recipes.size()); index++){
                    const Recipe& recipe = recipes[index];
                    const bool selected = index == recipeSelection;
                    const bool speedRecipe = recipe.owned == &speedLevel;
                    const bool maxed = recipe.cap > 0 && *recipe.owned >= recipe.cap;
                    if(split){
                        string line = (selected ? "> " : "  ") + to_string(index + 1) + ". " + recipe.name;
                        if(maxed)
                            line += "  MAX";
                        else
                            line += speedRecipe ? "  Lv" + to_string(*recipe.owned)
                                                : "  x" + to_string(*recipe.owned);
                        writeText(craftColumn, base + 2 + index * 3, line, craftWidth);
                        writeText(craftColumn, base + 3 + index * 3,
                                  "     " + to_string(recipe.costIron) + "Fe " +
                                  to_string(recipe.costCopper) + "Cu " +
                                  to_string(recipe.costTin) + "Sn", craftWidth);
                    }
                    else{
                        //窄窗口:配方压成一行,成本用斜杠分隔,放不下数量就不显示。
                        string line = (selected ? "> " : "  ") + to_string(index + 1) + ". " +
                                      recipe.name + " " + to_string(recipe.costIron) + "/" +
                                      to_string(recipe.costCopper) + "/" + to_string(recipe.costTin);
                        writeText(craftColumn, base + 1 + index, line, craftWidth);
                    }
                }
                const int noticeRow = split ? base + 8 : base + 3;
                writeText(craftColumn, noticeRow, craftNotice, craftWidth);
                if(split){
                    writeText(craftColumn, base + 10, "Up/Down Select  Enter Craft", craftWidth);
                    writeText(craftColumn, base + 11, "Left Game   Q Quit", craftWidth);
                }
                else{
                    writeText(craftColumn, base + 4, "Up/Down Craft  Left  Q Quit", craftWidth);
                }
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
