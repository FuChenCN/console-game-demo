#include "game/world.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace {
//网格是一维的,行优先。(x, y) 到数组下标的换算只允许出现在这一处。
inline size_t cellIndex(int x, int y, int width){
    return static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
}

struct PoolCandidate{
    int cx;
    int cy;
    int size;
};

//单个矿脉的格数范围。太小会变成一堆孤立点,太大在 512x512 上会互相抢地。
constexpr int POOL_MIN_CELLS = 30;
constexpr int POOL_MAX_CELLS = 159;
//中心被占时向外找空位的最大半径。
constexpr int POOL_CENTER_TRIES = 12;

constexpr int DIR_X[4] = {-1, 1, 0, 0};
constexpr int DIR_Y[4] = {0, 0, -1, 1};
}

void World::init(int w, int h, int rate, uint32_t s){
    if(w <= 0 || h <= 0)
        throw std::invalid_argument("world dimensions must be positive");
    width = w;
    height = h;
    resourceRate = rate;
    seed = s;
    rng.seed(s);
    relayout();
}

void World::relayout(){
    //重铺从同一序列头开始:改密度不会连带把位置也换一套。
    rng.seed(seed);
    cells.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
    oreLeft.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
    remainResource = 0;
    pools.clear();
    //旧矿池的 id 会全部重排,矿机不能跨图保留,重铺时一律拆掉。
    rigs.clear();

    const long long area = static_cast<long long>(width) * static_cast<long long>(height);
    long long target = area * resourceRate / 100;
    if(target > area)
        target = area;
    if(target <= 0)
        return;
    //target 数的是格子;remainResource 数的是数据量(每格 100),比较时换算一次。
    const long long targetUnits = target * ORE_PER_CELL;

    //候选矿脉:先按种子生成一批"中心 + 目标格数",再按密度激活前 N 条。
    //只生长激活的那几条,且只和已经激活的池抢地——所以提高密度时,
    //低密度那张图的每一格都原样留在高密度图里(前缀性质)。
    const int candidateCount = static_cast<int>(std::max(64LL, area / 40));
    std::vector<PoolCandidate> candidates;
    candidates.reserve(static_cast<size_t>(candidateCount));
    std::uniform_int_distribution<int> pickX(0, width - 1);
    std::uniform_int_distribution<int> pickY(0, height - 1);
    std::uniform_int_distribution<int> pickSize(POOL_MIN_CELLS, POOL_MAX_CELLS);
    for(int i = 0; i < candidateCount; i++)
        candidates.push_back(PoolCandidate{pickX(rng), pickY(rng), pickSize(rng)});

    long long planned = 0;
    int active = 0;
    while(active < static_cast<int>(candidates.size()) && planned < target){
        planned += candidates[active].size;
        active++;
    }

    std::uniform_int_distribution<int> pickDir(0, 3);
    for(int i = 0; i < active && remainResource < targetUnits; i++){
        const PoolCandidate& candidate = candidates[static_cast<size_t>(i)];

        //中心被别的池占了就沿方框向外找最近的空格;找不到就跳过这条候选。
        uint32_t seedCell = 0;
        bool placed = false;
        for(int radius = 0; radius <= POOL_CENTER_TRIES && !placed; radius++){
            for(int dy = -radius; dy <= radius && !placed; dy++){
                for(int dx = -radius; dx <= radius && !placed; dx++){
                    if(std::abs(dx) != radius && std::abs(dy) != radius)
                        continue;
                    int px = candidate.cx + dx;
                    int py = candidate.cy + dy;
                    if(px < 0 || py < 0 || px >= width || py >= height)
                        continue;
                    const size_t idx = cellIndex(px, py, width);
                    if(cells[idx] == 0){
                        seedCell = static_cast<uint32_t>(idx);
                        placed = true;
                    }
                }
            }
        }
        if(!placed)
            continue;
        //id 是 1 基的,cells 里 0 专门留给空格。
        if(pools.size() >= 65535u)
            break;
        const uint16_t id = static_cast<uint16_t>(pools.size() + 1);

        OrePool pool;
        pool.id = id;
        //类型按 id 均分:同一张图三类数量基本相等,且密度变化时前缀池的类型不变。
        pool.kind = static_cast<OreKind>((id - 1) % ORE_KIND_COUNT);
        pool.cx = static_cast<int>(seedCell % static_cast<uint32_t>(width));
        pool.cy = static_cast<int>(seedCell / static_cast<uint32_t>(width));
        pool.capacity = 0;
        pool.remaining = 0;
        pool.nextRegrowAt = 0;

        cells[seedCell] = id;
        oreLeft[seedCell] = ORE_PER_CELL;
        pool.cells.push_back(seedCell);
        remainResource += ORE_PER_CELL;

        //随机游走长团块:每次从已有格子里挑一格,往随机方向扩一格。
        int fails = 0;
        while(static_cast<int>(pool.cells.size()) < candidate.size &&
              fails < candidate.size * 4){
            std::uniform_int_distribution<size_t> pickCell(0, pool.cells.size() - 1);
            const uint32_t base = pool.cells[pickCell(rng)];
            const int bx = static_cast<int>(base % static_cast<uint32_t>(width));
            const int by = static_cast<int>(base / static_cast<uint32_t>(width));
            const int dir = pickDir(rng);
            const int nx = bx + DIR_X[dir];
            const int ny = by + DIR_Y[dir];
            if(nx < 0 || ny < 0 || nx >= width || ny >= height){
                fails++;
                continue;
            }
            const size_t ni = cellIndex(nx, ny, width);
            if(cells[ni] != 0){
                fails++;
                continue;
            }
            cells[ni] = id;
            oreLeft[ni] = ORE_PER_CELL;
            pool.cells.push_back(static_cast<uint32_t>(ni));
            remainResource += ORE_PER_CELL;
            fails = 0;
        }
        if(pool.cells.empty())
            continue;
        pool.capacity = static_cast<int>(pool.cells.size()) * ORE_PER_CELL;
        pool.remaining = pool.capacity;
        pools.push_back(std::move(pool));
    }

    //高密度下随机游走会被已占格切碎、长不满。把剩下的空格并给相邻的池,
    //这样 rate=100 仍然铺满整张图,密度语义和旧实现一致。
    if(remainResource < targetUnits){
        bool changed = true;
        bool reached = false;
        while(remainResource < targetUnits && changed && !reached){
            changed = false;
            for(int row = 0; row < height && !reached; row++){
                for(int column = 0; column < width; column++){
                    //补到目标就停,不能任由同一趟扫描靠连锁反应铺满全图。
                    if(remainResource >= targetUnits){
                        reached = true;
                        break;
                    }
                    const size_t idx = cellIndex(column, row, width);
                    if(cells[idx] != 0)
                        continue;
                    uint16_t id = 0;
                    if(column > 0 && cells[idx - 1] != 0)
                        id = cells[idx - 1];
                    else if(column + 1 < width && cells[idx + 1] != 0)
                        id = cells[idx + 1];
                    else if(row > 0 && cells[idx - width] != 0)
                        id = cells[idx - width];
                    else if(row + 1 < height && cells[idx + width] != 0)
                        id = cells[idx + width];
                    if(id == 0)
                        continue;
                    cells[idx] = id;
                    oreLeft[idx] = ORE_PER_CELL;
                    OrePool& pool = pools[id - 1];
                    pool.cells.push_back(static_cast<uint32_t>(idx));
                    pool.capacity += ORE_PER_CELL;
                    pool.remaining += ORE_PER_CELL;
                    remainResource += ORE_PER_CELL;
                    changed = true;
                }
            }
        }
    }
}

bool World::inBounds(int x, int y) const{
    return x >= 0 && x < width && y >= 0 && y < height;
}

bool World::hasResource(int x, int y) const{
    if(!inBounds(x, y))
        return false;
    return cells[cellIndex(x, y, width)] != 0;
}

uint16_t World::poolAt(int x, int y) const{
    if(!inBounds(x, y))
        return 0;
    return cells[cellIndex(x, y, width)];
}

int World::oreLeftAt(int x, int y) const{
    if(!inBounds(x, y))
        return 0;
    return static_cast<int>(oreLeft[cellIndex(x, y, width)]);
}

bool World::collectAt(int x, int y){
    if(!hasResource(x, y))
        return false;
    const size_t idx = cellIndex(x, y, width);
    const uint16_t id = cells[idx];
    oreLeft[idx]--;
    pools[id - 1].remaining--;
    remainResource--;
    if(oreLeft[idx] == 0)
        cells[idx] = 0;   //数据挖空了这格才消失
    return true;
}

int World::updatePools(unsigned long long now){
    int grown = 0;
    for(OrePool& pool : pools){
        if(pool.nextRegrowAt == 0){
            //第一次被驱动时才开始计时,World 自己不需要知道"开局时刻"。
            pool.nextRegrowAt = now + ORE_REGROW_MS;
            continue;
        }
        if(now < pool.nextRegrowAt)
            continue;
        //不追补:错过的份额不排成一串,下次仍然从当前时刻重新计 5 分钟。
        pool.nextRegrowAt = now + ORE_REGROW_MS;
        if(pool.remaining >= pool.capacity)
            continue;
        for(const uint32_t idx : pool.cells){
            if(oreLeft[idx] == 0){
                cells[idx] = pool.id;
                oreLeft[idx] = ORE_PER_CELL;
                pool.remaining += ORE_PER_CELL;
                remainResource += ORE_PER_CELL;
                grown++;
                break;
            }
        }
    }
    return grown;
}

bool World::placeRig(int x, int y, unsigned long long now, int stepMs){
    if(!inBounds(x, y) || stepMs <= 0)
        return false;
    const size_t idx = cellIndex(x, y, width);
    if(cells[idx] == 0)
        return false;
    for(const Rig& rig : rigs){
        if(rig.x == x && rig.y == y)
            return false;
    }
    //矿机只占这一格,不采地基:数据留在池里,落地后按正常节奏从脚下开始挖。
    rigs.push_back(Rig{x, y, cells[idx], now + static_cast<unsigned long long>(stepMs)});
    return true;
}

int World::advanceRigs(unsigned long long now, int stepMs){
    int collected = 0;
    for(Rig& rig : rigs){
        if(now < rig.nextAt)
            continue;
        //和旧机器人一样按当前时间重新排,卡顿掉的份额不追补。
        rig.nextAt = now + static_cast<unsigned long long>(stepMs);
        if(rig.pool == 0 || rig.pool > pools.size())
            continue;
        OrePool& pool = pools[rig.pool - 1];
        if(pool.remaining <= 0)
            continue;

        //取池子里离矿机最近的一格,每出手扣它 1 个数据;扣到 0 这格才消失。
        uint32_t best = 0;
        int bestDistance = std::numeric_limits<int>::max();
        for(const uint32_t idx : pool.cells){
            if(cells[idx] != pool.id)
                continue;
            const int cx = static_cast<int>(idx % static_cast<uint32_t>(width));
            const int cy = static_cast<int>(idx / static_cast<uint32_t>(width));
            const int distance = std::abs(cx - rig.x) + std::abs(cy - rig.y);
            if(distance < bestDistance){
                bestDistance = distance;
                best = idx;
            }
        }
        if(bestDistance == std::numeric_limits<int>::max())
            continue;
        oreLeft[best]--;
        pool.remaining--;
        remainResource--;
        if(oreLeft[best] == 0)
            cells[best] = 0;
        collected++;
    }
    return collected;
}
