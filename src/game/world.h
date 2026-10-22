#ifndef PYS_GAME_WORLD_H
#define PYS_GAME_WORLD_H

#include <cstdint>
#include <random>
#include <vector>

#include "game/types.h"

//地图默认规模。尺寸已经是运行期可变的,这里只是开局初始值——
//"世界规模选择"界面做出来之后,由玩家选中的宽高传进 World::init。
inline constexpr int DEFAULT_MAP_W = 512;
inline constexpr int DEFAULT_MAP_H = 512;

//矿脉参数。集中放这里是为了先跑通玩法;路线里"内容数据化"那一步会把它搬进外部表。
inline constexpr int DEPOSIT_MIN_CELLS = 32;      //一条脉最少占多少格
inline constexpr int DEPOSIT_MAX_CELLS = 128;     //最多占多少格
inline constexpr int ORE_PER_CELL = 100;          //每格贡献的储量:脉的总量 = 面积 x 100
inline constexpr int DEFAULT_COVERAGE = 8;        //全图被矿脉覆盖的百分比
inline constexpr int PLAYER_TAKE_MIN = 1;         //玩家手采一次的下限
inline constexpr int PLAYER_TAKE_MAX = 3;         //上限:手采也必须尊重"池"的余量
inline constexpr int BOT_TAKE = 1;                //机器人每步从池里取多少
inline constexpr int PROBE_HALF = 1;              //探测范围:以 @ 为中心,半边长 1 → 3x3
inline constexpr uint16_t NO_DEPOSIT = 0;         //格子里 0 表示"不是矿"

//一次迈步的结果。moved 表示这一帧是否真的动了(渲染靠它决定是否重绘),
//collected 是这一步从池里拿到的单位数——背包计数属于 Game,由调用方去加。
struct BotStep{
    bool moved;
    int collected;
};

//被模拟的世界:矿脉网格、密度、机器人。
//它不知道商店、界面、玩家背包和键盘,那些属于 Game;这一层只描述"世界怎么变"。
//
//模型: 矿以"脉"为单位,整条脉共用一个储量池。cells 只存归属 id,不存独立储量,
//所以采空是整条脉一起消失。矿脉列表只追加、永不删除,保证格子里的 id 永远有效。
struct World{
    int width = DEFAULT_MAP_W;
    int height = DEFAULT_MAP_H;

    std::vector<uint16_t> cells;   //行优先,长度 width*height;0 = 无矿,否则是 deposit id
    std::vector<Deposit> deposits; //下标即 id-1:id 从 1 开始,留出 0 表示"无"
    int coverage = DEFAULT_COVERAGE;
    std::vector<Bot> bots;
    unsigned long long botDeadline = 0;

    //随机源。seed 记下来是为了让同一张图能原样重建:调试面板会显示它,
    //以后存档也只需要存这个数,而不是整张网格。
    uint32_t seed = 0;
    //引擎选 minstd_rand:mt19937 的状态有 25KB,嵌进对象会把 World 撑大(有断言拦着)。
    std::minstd_rand rng;

    //分配网格并按覆盖率生成矿脉。宽高必须为正,否则抛 std::invalid_argument 且不动原状态。
    void init(int w, int h, int coveragePercent, uint32_t s);

    //只重新生成矿脉,尺寸不变(调试改密度时用)。
    void regenerate();

    //以下三个是网格读取的唯一入口。
    bool inBounds(int x, int y) const;
    int depositAt(int x, int y) const;             //0 = 不是矿
    Deposit* findDeposit(int id);                  //nullptr = id 不存在
    const Deposit* findDeposit(int id) const;

    //这一格该不该画成矿:要有脉,而且已经被探测到。渲染只问这一句,
    //省得每处各写一遍"有没有矿 + 探没探明"的组合判断。
    bool showsOre(int x, int y) const;

    //全图还剩多少矿(未枯竭脉的余量之和)。矿脉数量是几百这个量级,每帧调用也不贵。
    int remainingUnits() const;

    //从 (x,y) 所在脉的池里取货:未探明、已枯竭、格数不够都会算失败。
    //成功时返回实际拿到的单位数(池余量不足时会少于 requested)。
    int takeFrom(int x, int y, int requested);

    //采空一条脉:标记枯竭并把它的所有格子从网格撤走。只由 takeFrom 在余量归零时调用。
    void exhaust(Deposit& target);

    //玩家按 E:先掷 1~3 的目标量,再受池余量约束。返回拿到的单位数。
    int playerTake(int x, int y);

    //探测:揭示以 (cx,cy) 为中心、边长 2*PROBE_HALF+1 的正方形。返回本次新发现的脉数。
    int probeAt(int cx, int cy);

    //按已购数量补齐机器人。
    void syncBots(int ownedRobots);

    //到点让全体机器人各迈一步;没到点什么都不做。
    BotStep advanceBots(unsigned long long now, int stepMs);
};

#endif
