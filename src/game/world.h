#ifndef PYS_GAME_WORLD_H
#define PYS_GAME_WORLD_H

#include <vector>
#include <cstdlib>

#include "game/types.h"

//地图尺寸。既是 World 的维度,也是玩家坐标裁剪和视口计算的边界,
//所以放在头文件的命名空间作用域:各处引用 MAP_W / MAP_H 的写法保持不变。
inline constexpr int MAP_W = 128;
inline constexpr int MAP_H = 32;

//一次迈步的结果。moved 表示这一帧是否真的动了(渲染要靠它决定是否重绘),
//collected 是这一步吃掉的资源个数——背包计数属于 Game,所以由调用方去加。
struct BotStep{
    bool moved;
    int collected;
};

//被模拟的世界:资源网格、剩余量、密度、以及跑在上面的机器人。
//它不知道商店、界面、玩家背包和键盘,那些属于 Game;这一层只描述"世界怎么变"。
//
//不变量: remainResource 必须等于网格中 resource 为 true 的格数。
//网格和这个计数只能由下面四个方法成对改动——外部代码直接写 resource[...] 就会破坏它,
//而 refillIfSparse 的"见底就补"判定依赖这条(违反后补波会把计数越补越高)。
struct World{
    bool resource[MAP_H][MAP_W] = {};
    int remainResource = 0;
    int resourceRate = 6;                        //资源密度百分比,调试模式可实时改
    std::vector<Bot> bots;
    unsigned long long botDeadline = 0;          //下次允许迈步的时刻

    //按当前密度重铺整张地图并重算剩余量。开局铺图和调试调密度共用这一份实现。
    void layoutResource();

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
