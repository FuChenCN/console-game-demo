#ifndef PYS_GAME_WORLD_H
#define PYS_GAME_WORLD_H

#include <cstdint>
#include <random>
#include <vector>

#include "game/types.h"

//地图默认规模。尺寸已经是运行期可变的,这里只是开局初始值——
//等"世界规模选择"界面做出来,由玩家选中的宽高传进 World::init。
//512 x 512 = 262144 格,每格 2 字节约 512KB,所以网格只能放堆上(见 cells)。
inline constexpr int DEFAULT_MAP_W = 512;
inline constexpr int DEFAULT_MAP_H = 512;

//矿池的再生节奏:每个池固定每 5 分钟回 1 格,回满为止。
//节奏写在 World 里而不是 main 里,是为了让测试能直接按毫秒驱动它。
inline constexpr unsigned long long ORE_REGROW_MS = 300ull * 1000ull;

//一片矿脉。cells 里存的就是它的 id(1..N),0 表示空格。
//足迹单独留一份:重生时按足迹回填,不用全图扫描——这正是旧 refillIfSparse 的替换点。
struct OrePool{
    uint16_t id;
    int cx;
    int cy;
    int capacity;                     //足迹格数,恒等于 cells.size()
    int remaining;                    //足迹里还有矿的格数
    std::vector<uint32_t> cells;      //足迹(一维下标),生成顺序 + 后来补进来的格子
    unsigned long long nextRegrowAt;  //0 = 还没开始计时
};

//放置式矿机:占一格,每隔 stepMs 从所属矿池里取走一格最近的矿。
struct Rig{
    int x;
    int y;
    uint16_t pool;                    //所属矿池 id
    unsigned long long nextAt;        //下次允许出手的时刻
};

//被模拟的世界:矿脉网格、剩余量、密度、矿机。
//它不知道商店、界面、玩家背包和键盘,那些属于 Game;这一层只描述"世界怎么变"。
//
//不变量: remainResource 必须等于网格中"有资源"的格数,也必须等于
//所有矿池 remaining 之和。网格、计数、矿池的 remaining 只能由下面的方法成对改动。
struct World{
    int width = DEFAULT_MAP_W;
    int height = DEFAULT_MAP_H;

    //0 = 空,否则是矿池 id(1..pools.size())。uint16_t 是给矿脉预留的宽度,
    //候选池数量封顶在 65535 以下,所以不用改成 uint32_t。
    std::vector<uint16_t> cells;

    int remainResource = 0;
    int resourceRate = 6;                        //资源密度百分比,调试模式可实时改
    std::vector<OrePool> pools;
    std::vector<Rig> rigs;

    //种子用于重建初始矿脉;存档还需记录采集、重生等变化及当前随机引擎状态。
    uint32_t seed = 0;
    //minstd_rand 的状态较小,避免随机引擎把 World 对象本身撑大。
    std::minstd_rand rng;                          //只允许由下面的方法使用,外部别碰

    //按指定规模和种子分配网格并铺图。开局、以及以后"选完世界规模"都走这里。
    //宽高必须为正数;否则抛出 std::invalid_argument,原世界(包括随机源)保持不变。
    void init(int w, int h, int rate, uint32_t s);

    //从保存的种子重播,按当前密度重铺整张图并重建矿池。
    //重铺会清空矿机:旧池 id 作废,矿机不能跟着换池。
    void relayout();

    bool inBounds(int x, int y) const;

    //这一格有没有资源。注意参数顺序是 (x, y),内部才换算成 (行, 列)。
    bool hasResource(int x, int y) const;

    //这一格属于哪个矿池;空格或越界返回 0。
    uint16_t poolAt(int x, int y) const;

    //采下脚下这一格:只有真的有资源才改动世界并返回 true。
    bool collectAt(int x, int y);

    //每 5 分钟给每个未满的池补一格;返回这次补了多少格。
    //没有全图扫描:只按各池足迹找第一格空的。
    int updatePools(unsigned long long now);

    //在脚下有矿的格子上放一台矿机:吃掉这格矿当地基。
    //没有矿、越界、或这格已经有矿机时返回 false。
    bool placeRig(int x, int y, unsigned long long now, int stepMs);

    //到点的矿机各取走一格最近的矿;返回这次一共取了多少格。
    int advanceRigs(unsigned long long now, int stepMs);
};

#endif
