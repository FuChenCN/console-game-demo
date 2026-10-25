#ifndef PYS_GAME_WORLD_H
#define PYS_GAME_WORLD_H

#include <cstdint>
#include <random>
#include <vector>

//地图默认规模。尺寸已经是运行期可变的,这里只是开局初始值——
//等"世界规模选择"界面做出来,由玩家选中的宽高传进 World::init。
//512 x 512 = 262144 格,每格 2 字节约 512KB,所以网格只能放堆上(见 cells)。
inline constexpr int DEFAULT_MAP_W = 512;
inline constexpr int DEFAULT_MAP_H = 512;

//矿池的再生节奏:每个池固定每 5 分钟补 1 格(100 个数据),补满为止。
//节奏写在 World 里而不是 main 里,是为了让测试能直接按毫秒驱动它。
inline constexpr unsigned long long ORE_REGROW_MS = 300ull * 1000ull;

//一个矿格能保存的数据量:每次采集/矿机出手扣 1,扣到 0 这格才消失。
inline constexpr int ORE_PER_CELL = 100;

//矿脉类型。一个池从头到尾只属于一种矿;目前类型只决定颜色,
//三类的数据量/单价都一样,后续要做冶炼或分仓时再从这里接。
enum class OreKind : uint8_t{
    Iron = 0,     //灰
    Copper = 1,   //橙
    Tin = 2,      //青灰
    Stone = 3,    //土黄,显示 ST
    Coal = 4,     //煤,C
};

inline constexpr int ORE_KIND_COUNT = 5;

//一次出手挖到的东西,按矿种分开计数。玩家一次只挖 1 个,矿机一次也只挖 1 个,
//但接口按"一捆产出"来设计,以后加范围开采或双倍产出不用改签名。
struct OreYield{
    int iron = 0;
    int copper = 0;
    int tin = 0;
    int stone = 0;
    int coal = 0;

    int total() const{ return iron + copper + tin + stone + coal; }

    void add(OreKind kind, int amount = 1){
        switch(kind){
        case OreKind::Iron: iron += amount; break;
        case OreKind::Copper: copper += amount; break;
        case OreKind::Tin: tin += amount; break;
        case OreKind::Stone: stone += amount; break;
        case OreKind::Coal: coal += amount; break;
        }
    }
};

//一片矿脉。cells 里存的就是它的 id(1..N),0 表示空格。
//足迹单独留一份:重生时按足迹回填,不用全图扫描——这正是旧 refillIfSparse 的替换点。
struct OrePool{
    uint16_t id;
    OreKind kind;                     //矿脉类型(铁/铜/锡),决定颜色
    int cx;
    int cy;
    int capacity;                     //数据总容量 = 足迹格数 * ORE_PER_CELL
    int remaining;                    //剩余数据量,范围 0..capacity
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
//它不知道背包、制作、界面和键盘,那些属于 Game;这一层只描述"世界怎么变"。
//
//不变量: cells[i] != 0 当且仅当 oreLeft[i] > 0(cells 是矿池 id,oreLeft 是 1..100 的数据量);
//remainResource 必须等于所有 oreLeft 之和,也必须等于所有矿池 remaining 之和。
//网格、格子的数据量、计数只能由下面的方法成对改动。
struct World{
    int width = DEFAULT_MAP_W;
    int height = DEFAULT_MAP_H;

    //0 = 空,否则是矿池 id(1..pools.size())。uint16_t 是给矿脉预留的宽度,
    //候选池数量封顶在 65535 以下,所以不用改成 uint32_t。
    std::vector<uint16_t> cells;
    //每个矿格还剩多少数据(1..ORE_PER_CELL)。cells 非零时这里一定大于 0;
    //挖到 0 时 cells 同步清零,两格数组永远同进同退。
    std::vector<uint8_t> oreLeft;

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

    //这一格还剩多少数据(0..ORE_PER_CELL);空格或越界返回 0。
    int oreLeftAt(int x, int y) const;

    //这一格属于哪种矿;只有 hasResource 为真时返回 true。
    bool oreKindAt(int x, int y, OreKind& kind) const;

    //采下脚下这一格:只有真的有资源才改动世界并返回 true。
    bool collectAt(int x, int y);

    //每 5 分钟给每个未满的池补一格(新格带满 100 数据);返回这次补了几格。
    //没有全图扫描:只按各池足迹找第一格空的。
    int updatePools(unsigned long long now);

    //在脚下有矿的格子上放一台矿机:只占位,不动这格的数据,落地后从脚下开始挖。
    //没有矿、越界、或这格已经有矿机时返回 false。
    bool placeRig(int x, int y, unsigned long long now, int stepMs);

    //到点的矿机各取走 1 个数据;返回这次按矿种分开的产出。
    OreYield advanceRigs(unsigned long long now, int stepMs);
};

#endif
