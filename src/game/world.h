#ifndef PYS_GAME_WORLD_H
#define PYS_GAME_WORLD_H

#include <cstdint>
#include <cstdlib>
#include <vector>

#include "game/types.h"

//地图默认规模。尺寸已经是运行期可变的,这里只是开局初始值——
//等"世界规模选择"界面做出来,由玩家选中的宽高传进 World::init。
//512 x 512 = 262144 格,每格 2 字节约 512KB,所以网格只能放堆上(见 cells)。
inline constexpr int DEFAULT_MAP_W = 512;
inline constexpr int DEFAULT_MAP_H = 512;

//一次迈步的结果。moved 表示这一帧是否真的动了(渲染要靠它决定是否重绘),
//collected 是这一步吃掉的资源个数——背包计数属于 Game,所以由调用方去加。
struct BotStep{
    bool moved;
    int collected;
};

//被模拟的世界:网格、剩余量、密度、以及跑在上面的机器人。
//它不知道商店、界面、玩家背包和键盘,那些属于 Game;这一层只描述"世界怎么变"。
//
//不变量: remainResource 必须等于网格中"有资源"的格数。
//网格和这个计数只能由下面的方法成对改动——外部代码直接写 cells[...] 就会破坏它,
//而 refillIfSparse 的"见底就补"判定依赖这条(违反后补波会把计数越补越高)。
struct World{
    int width = DEFAULT_MAP_W;
    int height = DEFAULT_MAP_H;

    //格子内容。现阶段只有 0(空)和 1(有资源)两种用法;下一步做矿脉时,这里改存
    //格子的归属 id,存储宽度已经提前留好,到时候不必再改内存布局。
    std::vector<uint16_t> cells;

    int remainResource = 0;
    int resourceRate = 6;                        //资源密度百分比,调试模式可实时改
    std::vector<Bot> bots;
    unsigned long long botDeadline = 0;          //下次允许迈步的时刻

    //按指定规模分配网格并按当前密度铺图。开局、以及以后"选完世界规模"都走这里。
    //宽高必须为正数;否则抛出 std::invalid_argument,原世界状态保持不变。
    void init(int w, int h, int rate);

    //按当前密度重铺整张图,不重新分配尺寸(调试改密度时用)。
    void relayout();

    bool inBounds(int x, int y) const;

    //这一格有没有资源。注意参数顺序是 (x, y),内部才换算成 (行, 列)。
    bool hasResource(int x, int y) const;

    //采下脚下这一格:只有真的有资源才改动世界并返回 true。
    bool collectAt(int x, int y);

    //剩余量见底时补一波,不改背包。
    void refillIfSparse();

    //按已购数量补齐机器人:一只一行,从第一行开始往下排。
    void syncBots(int ownedRobots);

    //到点就让全体机器人各迈一步;没到点什么都不做。
    BotStep advanceBots(unsigned long long now, int stepMs);
};

#endif
