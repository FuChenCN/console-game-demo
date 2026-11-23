#ifndef PYS_GAME_WORLD_H
#define PYS_GAME_WORLD_H

#include <cstdint>
#include <random>
#include <vector>

#include "game/types.h"

//地图默认规模。尺寸是运行期的:这里只是开局初始值,以后"世界规模选择"界面把玩家选的宽高传进 init。
inline constexpr int DEFAULT_MAP_W = 512;
inline constexpr int DEFAULT_MAP_H = 512;

//矿脉参数。集中放这里是为了先跑通玩法,"内容数据化"那一步会搬进外部表。
inline constexpr int DEPOSIT_MIN_CELLS = 32;      //一条脉最少占多少格
inline constexpr int DEPOSIT_MAX_CELLS = 128;     //最多占多少格
inline constexpr int ORE_PER_CELL = 100;          //每格贡献的储量:脉的总量 = 面积 x 100
inline constexpr int DEFAULT_COVERAGE = 8;        //全图被矿脉覆盖的百分比
inline constexpr int PLAYER_TAKE_MIN = 1;         //玩家手采一次的下限
inline constexpr int PLAYER_TAKE_MAX = 3;         //上限:手采也必须尊重"池"的余量
inline constexpr int PROBE_HALF = 1;              //探测范围:以 @ 为中心,半边长 1 → 3x3
inline constexpr uint16_t NO_DEPOSIT = 0;         //格子里 0 表示"不是矿"

//矿机参数。缓存是"容量上限",不是攒满才能取:玩家随时可以掏空它。
inline constexpr int MINER_BUFFER = 30;           //缓存上限 #,装满即停机
inline constexpr int MINER_TAKE = 1;              //每拍从池里取多少 #
inline constexpr int SPEED_MAX = 4;               //速度升级最高第 4 档
//五档节拍(毫秒),下标是已购档数。基准 0.5s 一拍,买到顶 0.05s。
//矿机不移动,所以这张表作用于"取矿的节奏",不是移动速度。
inline constexpr int MINER_SPEED_MS[SPEED_MAX + 1] = {500, 375, 250, 125, 50};

//放置结果。把失败原因分出来,是因为"放不下去"必须能告诉玩家是哪一种,而不是只说"不行"。
enum class PlaceResult{
    placed,          //已放下
    out_of_bounds,   //3x2 占地会伸出地图
    no_vein,         //占地里有的格子不带矿脉
    not_one_vein,    //占地跨了两条矿脉
    undiscovered,    //脚下这条脉还没探明
    blocked,         //已被别的矿机占着
};

//一次放置尝试的结果,带消息用的落点。
struct PlaceOutcome{
    PlaceResult result;
    int minerId;     //成功时的 id,失败为 0
};

//被模拟的世界:矿脉网格与储量池、放在上面的矿机。
//它不知道商店、界面、玩家背包和键盘,那些属于 Game;这一层只描述"世界怎么变"。
//
//不变量一: 矿脉和矿机的列表都只追加、只标记移除,永不真删除。格子里存的就是它们的 id,
//          删除条目会让所有格子指向错对象。拆除的槽位会被下一次放置复用。
//不变量二: 一条脉的 remaining 等于它当前覆盖格子应有的量,采空即整条撤走。
struct World{
    int width = DEFAULT_MAP_W;
    int height = DEFAULT_MAP_H;

    std::vector<uint16_t> cells;          //行优先,长度 width*height;0 = 无矿,否则是 deposit id
    std::vector<uint16_t> machineCells;  //同尺寸;0 = 此格无矿机,否则是 miner id
    std::vector<uint16_t> shrinkRank;    //同尺寸;0 = 不是矿格,1.. = 被挖空时从外缘剥落的顺序
    std::vector<Deposit> deposits;        //下标 = id-1
    std::vector<Miner> miners;            //下标 = id-1
    int coverage = DEFAULT_COVERAGE;
    unsigned long long minerDeadline = 0; //下次允许全体矿机各取一拍的时机

    //随机源。seed 记下来是为了让同一张图能原样重建:调试面板显示它,
    //以后存档也只需要存这个数,而不是整张网格。
    uint32_t seed = 0;
    //引擎选 minstd_rand:mt19937 的状态有 25KB,嵌进对象会把 World 撑大(有断言拦着)。
    std::minstd_rand rng;

    //分配网格并生成矿脉。宽高必须为正,否则抛 std::invalid_argument 且不动原状态。
    void init(int w, int h, int coveragePercent, uint32_t s);
    //只重新生成矿脉,尺寸不变(调试改覆盖率时用)。会清空矿机。
    void regenerate();

    //网格读取的唯一入口。
    bool inBounds(int x, int y) const;
    int depositAt(int x, int y) const;
    Deposit* findDeposit(int id);
    const Deposit* findDeposit(int id) const;
    //这一格该不该画成矿:要有脉、已被探测到,而且还没被"剥掉"。
    //池子每少 100#,从外缘随机剥掉一格 #;剥落顺序在生成时定好,采空时整条脉消失。
    bool showsOre(int x, int y) const;
    //全图还剩多少 #(活脉余量之和)。矿脉是几百这个量级,每帧调用也不贵。
    int remainingUnits() const;

    //从 (x,y) 所在脉的池里取货:未探明、已枯竭都会失败。
    //返回实际拿到的单位数(池余量不足时会少于 requested)。
    int takeFrom(int x, int y, int requested);
    void exhaust(Deposit& target);        //采空:标记枯竭并把整条脉从网格撤走
    int playerTake(int x, int y);         //玩家按 E:掷 1~3 再受余量约束
    int probeAt(int cx, int cy);          //揭示 3x3 内首次碰到的脉,返回新发现条数

    //矿机。放置只校验几何与矿脉状态,库存增减归 Game 管。
    PlaceOutcome tryPlaceMiner(int x, int y);
    Miner* minerAt(int x, int y);
    const Miner* minerAt(int x, int y) const;
    int retrieveAt(int x, int y);         //取货:清空这台机器的缓存,返回取到的 #;之后它恢复工作
    bool removeMinerAt(int x, int y);     //拆回:腾出占地,槽位可复用
    //到点让每台矿机各取一拍。返回是否真的推进了节拍。
    bool advanceMiners(unsigned long long now, int stepMs);
};

#endif
