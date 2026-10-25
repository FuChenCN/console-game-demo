//针对真实 World 实现的行为测试:直接链接 src/game/world.cpp,不开终端、不画屏幕。
#include "game/world.h"

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <stdexcept>

static int failures = 0;
static void check(const char* what, bool ok){
    printf("  %s  %s\n", ok ? "PASS" : "FAIL", what);
    if(!ok) failures++;
}

static int countCellsOf(const World& w, uint16_t id){
    int n = 0;
    for(const uint16_t cell : w.cells)
        if(cell == id) n++;
    return n;
}

static int countOreCells(const World& w){
    int n = 0;
    for(const uint16_t cell : w.cells)
        if(cell != 0) n++;
    return n;
}

static int sumOreLeft(const World& w){
    int sum = 0;
    for(const uint8_t amount : w.oreLeft)
        sum += amount;
    return sum;
}

static int sumRemaining(const World& w){
    int sum = 0;
    for(const OrePool& pool : w.pools)
        sum += pool.remaining;
    return sum;
}

static int cellX(const World& w, uint32_t idx){
    return static_cast<int>(idx % static_cast<uint32_t>(w.width));
}

static int cellY(const World& w, uint32_t idx){
    return static_cast<int>(idx / static_cast<uint32_t>(w.width));
}

//把一格从 100 挖到 0,返回挖出来的数据量。
static int drainCell(World& w, int x, int y){
    int n = 0;
    while(w.collectAt(x, y))
        n++;
    return n;
}

static void drainPool(World& w, OrePool& pool){
    for(const uint32_t idx : pool.cells)
        drainCell(w, cellX(w, idx), cellY(w, idx));
}

//池子里离矿机最近的一格。advanceRigs 的取矿顺序要和它一致。
static uint32_t nearestCell(const World& w, const OrePool& pool, int x, int y){
    uint32_t best = 0;
    int bestDistance = 1 << 30;
    for(const uint32_t idx : pool.cells){
        if(w.cells[idx] != pool.id) continue;
        const int cx = cellX(w, idx);
        const int cy = cellY(w, idx);
        const int distance = (cx > x ? cx - x : x - cx) + (cy > y ? cy - y : y - cy);
        if(distance < bestDistance){
            bestDistance = distance;
            best = idx;
        }
    }
    return best;
}

int main(){
    printf("=== map is reproducible from its seed ===\n");
    {
        World a, b;
        a.init(64, 32, 30, 12345u);
        b.init(64, 32, 30, 12345u);
        check("same seed gives byte-identical maps", a.cells == b.cells);
        check("same seed gives identical per-cell data", a.oreLeft == b.oreLeft);
        check("and the same remaining count", a.remainResource == b.remainResource);
        check("and the same number of pools", a.pools.size() == b.pools.size());
        check("seed is recorded on the object", a.seed == 12345u);

        World c;
        c.init(64, 32, 30, 99999u);
        check("a different seed gives a different map", a.cells != c.cells);

        //重铺从同一序列头重播,所以改密度不会顺手把已有资源换个位置。
        a.relayout();
        check("relayout replays the same sequence", a.cells == b.cells);

        //同一条随机序列、只把门槛放宽,低密度的格子必然是高密度的子集。
        World loose;
        loose.init(64, 32, 30, 12345u);
        loose.resourceRate = 60;
        loose.relayout();
        bool superset = true;
        for(size_t i = 0; i < b.cells.size(); i++)
            if(b.cells[i] != 0 && loose.cells[i] == 0)
                superset = false;
        check("raising density keeps every previous cell", superset);
        check("and adds more", loose.remainResource > b.remainResource);

        printf("  INFO  seed 12345 at rate=30%% on 64x32 -> remain = %d, pools = %zu\n",
               b.remainResource, b.pools.size());
    }

    printf("\n=== invalid dimensions are rejected without changing the world ===\n");
    {
        World w;
        w.init(8, 4, 100, 12345u);
        w.updatePools(5000);
        const uint32_t idx = w.pools.front().cells.front();
        const int x = cellX(w, idx);
        const int y = cellY(w, idx);
        check("rig can be placed before the invalid calls", w.placeRig(x, y, 6000, 1000) == true);

        const auto originalCells = w.cells;
        const auto originalOre = w.oreLeft;
        const auto originalPools = w.pools;
        const auto originalRigs = w.rigs;
        const auto originalSeed = w.seed;
        const auto originalRng = w.rng;
        const int originalRemain = w.remainResource;
        const int invalidSizes[][2] = {{0, 4}, {8, 0}, {-1, 4}, {8, -1}};
        for(const auto& size : invalidSizes){
            printf("  input: %d x %d\n", size[0], size[1]);
            bool rejected = false;
            try{
                w.init(size[0], size[1], 0, 99999u);
            }
            catch(const std::invalid_argument&){
                rejected = true;
            }
            check("nonpositive size throws invalid_argument", rejected);
            check("dimensions and grid stay unchanged",
                  w.width == 8 && w.height == 4 && w.cells == originalCells);
            check("per-cell data stays unchanged", w.oreLeft == originalOre);
            check("resource bookkeeping stays unchanged",
                  w.resourceRate == 100 && w.remainResource == originalRemain);
            check("seed and random engine stay unchanged",
                  w.seed == originalSeed && w.rng == originalRng);
            check("pools stay unchanged", w.pools.size() == originalPools.size());
            check("pool contents stay unchanged",
                  w.pools.front().remaining == originalPools.front().remaining &&
                  w.pools.front().capacity == originalPools.front().capacity &&
                  w.pools.front().nextRegrowAt == originalPools.front().nextRegrowAt);
            check("rigs stay unchanged",
                  w.rigs.size() == originalRigs.size() &&
                  w.rigs.front().x == originalRigs.front().x &&
                  w.rigs.front().y == originalRigs.front().y &&
                  w.rigs.front().pool == originalRigs.front().pool &&
                  w.rigs.front().nextAt == originalRigs.front().nextAt);
        }
    }

    printf("\n=== storage is on the heap and sized at runtime ===\n");
    {
        World w;
        w.init(64, 16, 0, 1u);
        check("object itself stays small", sizeof(World) < 512);
        check("cells sized width*height", w.cells.size() == 64 * 16);
        check("per-cell data sized width*height", w.oreLeft.size() == 64 * 16);
        check("runtime size honored", w.width == 64 && w.height == 16);
        check("rate=0 makes no pools", w.pools.empty());

        World big;
        big.init(512, 512, 0, 1u);
        check("same code handles 512x512", big.cells.size() == 512u * 512u);
        big.init(64, 64, 0, 1u);
        check("init clears pools from the previous world", big.pools.empty());
        check("init clears rigs from the previous world", big.rigs.empty());
    }

    printf("\n=== bounds are safe ===\n");
    {
        World w;
        w.init(8, 4, 0, 1u);
        check("inBounds rejects negative", w.inBounds(-1, 0) == false);
        check("inBounds rejects far x", w.inBounds(8, 0) == false);
        check("inBounds rejects far y", w.inBounds(0, 4) == false);
        check("inBounds accepts the corner", w.inBounds(7, 3) == true);
        check("hasResource off the map returns false, no crash", w.hasResource(9999, 9999) == false);
        check("poolAt off the map returns 0", w.poolAt(-1, 0) == 0);
        check("oreLeftAt off the map returns 0", w.oreLeftAt(-1, 0) == 0);
        check("collectAt off the map is a no-op", w.collectAt(-3, 7) == false);
        check("placeRig off the map is a no-op", w.placeRig(-1, 2, 0, 100) == false);
    }

    printf("\n=== pools agree with the grid, the data and the remaining count ===\n");
    {
        World w;
        w.init(64, 32, 25, 777u);
        check("pools exist at 25%%", !w.pools.empty());
        int summed = 0;
        bool idsOk = true;
        bool footprintsOk = true;
        bool sizesOk = true;
        bool dataOk = true;
        for(size_t i = 0; i < w.pools.size(); i++){
            const OrePool& pool = w.pools[i];
            if(pool.id != static_cast<uint16_t>(i + 1)) idsOk = false;
            if(pool.capacity != static_cast<int>(pool.cells.size()) * ORE_PER_CELL) sizesOk = false;
            if(pool.remaining > pool.capacity) sizesOk = false;
            if(pool.remaining != countCellsOf(w, pool.id) * ORE_PER_CELL) footprintsOk = false;
            int units = 0;
            for(const uint32_t idx : pool.cells){
                if(w.cells[idx] != pool.id) footprintsOk = false;
                if(w.oreLeft[idx] != ORE_PER_CELL) dataOk = false;
                units += w.oreLeft[idx];
            }
            if(units != pool.remaining) footprintsOk = false;
            summed += pool.remaining;
        }
        check("ids are 1..N in order", idsOk);
        check("capacity is 100 per footprint cell", sizesOk);
        check("footprint covers exactly the pool's cells", footprintsOk);
        check("every fresh cell holds 100 units", dataOk);
        check("sum of pool remaining equals remainResource", summed == w.remainResource);
        check("remainResource equals the sum of per-cell data", w.remainResource == sumOreLeft(w));
        check("each cell is 100 units initially", w.remainResource == countOreCells(w) * ORE_PER_CELL);
    }

    printf("\n=== one ore cell can be mined 100 times ===\n");
    {
        World w;
        w.init(32, 8, 100, 1u);
        OrePool& pool = w.pools.front();
        const uint32_t idx = pool.cells.front();
        const int x = cellX(w, idx);
        const int y = cellY(w, idx);
        const int poolBefore = pool.remaining;
        const int remainBefore = w.remainResource;
        check("cell starts with 100 units", w.oreLeftAt(x, y) == ORE_PER_CELL);

        int picks = 0;
        for(int i = 0; i < ORE_PER_CELL - 1; i++){
            if(w.collectAt(x, y)) picks++;
        }
        check("99 picks all succeed", picks == ORE_PER_CELL - 1);
        check("cell is still there after 99 picks", w.hasResource(x, y) == true);
        check("cell has one unit left", w.oreLeftAt(x, y) == 1);
        check("pool lost 99 units", pool.remaining == poolBefore - (ORE_PER_CELL - 1));
        check("remain lost 99 units", w.remainResource == remainBefore - (ORE_PER_CELL - 1));

        check("the 100th pick succeeds", w.collectAt(x, y) == true);
        check("cell disappears after the 100th pick", w.hasResource(x, y) == false);
        check("cell data is zero", w.oreLeftAt(x, y) == 0);
        check("pool lost a full cell", pool.remaining == poolBefore - ORE_PER_CELL);
        check("remain lost a full cell", w.remainResource == remainBefore - ORE_PER_CELL);
        check("the 101st pick fails", w.collectAt(x, y) == false);
    }

    printf("\n=== updatePools: one full cell per pool per 5 minutes ===\n");
    {
        World w;
        w.init(32, 8, 100, 1u);
        check("regrow interval is 300s", ORE_REGROW_MS == 300ull * 1000ull);

        OrePool& pool = w.pools.front();
        const uint32_t idx = pool.cells.front();
        const int x = cellX(w, idx);
        const int y = cellY(w, idx);
        check("drained cell yields exactly 100 units", drainCell(w, x, y) == ORE_PER_CELL);
        check("pool is one cell short", pool.remaining == pool.capacity - ORE_PER_CELL);

        const unsigned long long now = 10000;
        check("first update only starts the clock", w.updatePools(now) == 0);
        check("deadline is now + 5min", pool.nextRegrowAt == now + ORE_REGROW_MS);
        check("just before the deadline: nothing", w.updatePools(now + ORE_REGROW_MS - 1) == 0);

        pool.nextRegrowAt = now;
        check("at the deadline: exactly one cell", w.updatePools(now) == 1);
        check("cell is back", w.hasResource(x, y) == true);
        check("and comes back full", w.oreLeftAt(x, y) == ORE_PER_CELL);
        check("pool refilled by 100 units", pool.remaining == pool.capacity);
        check("next deadline rescheduled", pool.nextRegrowAt == now + ORE_REGROW_MS);

        check("a full pool does not grow", w.updatePools(now + ORE_REGROW_MS) == 0);

        //只挖掉部分数据时没有空格可补:回填只补整个格子,不会给半格续数。
        w.collectAt(x, y);
        pool.nextRegrowAt = now;
        check("a partially mined cell is not topped up", w.updatePools(now) == 0);
        check("its remaining data is untouched", w.oreLeftAt(x, y) == ORE_PER_CELL - 1);
    }

    printf("\n=== density still behaves statistically ===\n");
    {
        World w;
        w.init(64, 64, 100, 1u);
        check("rate=100 fills every cell", w.remainResource == 64 * 64 * ORE_PER_CELL);
        check("rate=100 leaves no empty cell", countOreCells(w) == 64 * 64);
        w.resourceRate = 0;
        w.relayout();
        check("rate=0 empties every cell", w.remainResource == 0);
        check("rate=0 leaves no pools", w.pools.empty());
        w.resourceRate = 30;
        w.relayout();
        double actual = countOreCells(w) * 100.0 / (w.width * w.height);
        printf("  INFO  rate=30%% measured %.2f%% of cells (fixed seed, so this number is stable)\n", actual);
        check("rate=30% lands in a sane band", actual > 25.0 && actual < 35.0);
        check("every filled cell carries 100 units", w.remainResource == countOreCells(w) * ORE_PER_CELL);
        check("remain equals the sum of pool remaining", sumRemaining(w) == w.remainResource);
    }

    printf("\n=== placeRig: keeps the ore under the rig, refuses invalid spots ===\n");
    {
        World w;
        w.init(40, 8, 100, 1u);
        OrePool& pool = w.pools.front();
        const uint32_t idx = pool.cells.front();
        const int x = cellX(w, idx);
        const int y = cellY(w, idx);
        const int remainBefore = w.remainResource;
        const int poolBefore = pool.remaining;
        const unsigned long long now = 1000;

        check("placement on an ore cell succeeds", w.placeRig(x, y, now, 1000) == true);
        check("ore under the rig is untouched", w.oreLeftAt(x, y) == ORE_PER_CELL);
        check("placement does not change the balance", w.remainResource == remainBefore);
        check("placement does not change the pool", pool.remaining == poolBefore);
        check("one rig stored", w.rigs.size() == 1);
        check("rig remembers its pool", w.rigs[0].pool == pool.id);
        check("rig first step is one interval away", w.rigs[0].nextAt == now + 1000);
        check("the same cell cannot host a second rig", w.placeRig(x, y, now, 1000) == false);
        check("zero step is rejected", w.placeRig(x, y, now, 0) == false);

        World empty;
        empty.init(32, 8, 0, 1u);
        check("placement on empty ground fails", empty.placeRig(0, 0, 0, 1000) == false);

        //半格矿:先挖掉 30,再放矿机,剩下的 70 留在池里,等矿机自己挖。
        World partial;
        partial.init(40, 8, 100, 1u);
        OrePool& partialPool = partial.pools.front();
        const uint32_t pidx = partialPool.cells.front();
        const int px = cellX(partial, pidx);
        const int py = cellY(partial, pidx);
        for(int i = 0; i < 30; i++)
            partial.collectAt(px, py);
        check("partially mined cell keeps 70 units", partial.oreLeftAt(px, py) == 70);
        check("placement succeeds on a partial cell", partial.placeRig(px, py, 0, 1000) == true);
        check("the 70 units stay in the pool",
              partial.hasResource(px, py) == true && partial.oreLeftAt(px, py) == 70);
    }

    printf("\n=== advanceRigs: one unit per step, nearest cell, timing gate ===\n");
    {
        World w;
        w.init(40, 8, 100, 1u);
        OrePool& pool = w.pools.front();
        const uint32_t idx = pool.cells.front();
        const int x = cellX(w, idx);
        const int y = cellY(w, idx);
        check("rig placed", w.placeRig(x, y, 1000, 1000) == true);
        Rig& rig = w.rigs[0];

        const uint32_t expected = nearestCell(w, pool, rig.x, rig.y);
        const int poolBefore = pool.remaining;
        const int remainBefore = w.remainResource;
        check("expected target still has data", w.oreLeft[expected] > 0);

        check("before the deadline: nothing", w.advanceRigs(1999, 1000) == 0);
        check("at the deadline: one unit", w.advanceRigs(2000, 1000) == 1);
        check("the nearest cell lost one unit", w.oreLeft[expected] == ORE_PER_CELL - 1);
        check("the nearest cell is still there", w.cells[expected] == pool.id);
        check("pool remaining decremented by one", pool.remaining == poolBefore - 1);
        check("remain decremented by one", w.remainResource == remainBefore - 1);
        check("next step rescheduled from now", rig.nextAt == 3000);

        //再挖 99 次,这格才真正消失。
        unsigned long long now = 3000;
        int mined = 0;
        for(int i = 0; i < ORE_PER_CELL - 1; i++){
            rig.nextAt = now;
            mined += w.advanceRigs(now, 1000);
            now += 1000;
        }
        check("the remaining 99 units are mined", mined == ORE_PER_CELL - 1);
        check("the cell finally disappears", w.oreLeft[expected] == 0);
        check("pool remaining tracks the whole cell", pool.remaining == poolBefore - ORE_PER_CELL);

        drainPool(w, pool);
        check("pool drained", pool.remaining == 0);
        rig.nextAt = 100000;
        check("idle rig mines nothing", w.advanceRigs(100000, 1000) == 0);
        check("idle rig still reschedules", rig.nextAt == 101000);

        //池子回一格(满 100)之后,矿机又能接着挖。
        pool.nextRegrowAt = 200000;
        check("one cell regrown", w.updatePools(200000) == 1);
        rig.nextAt = 200000;
        check("rig picks the regrown cell", w.advanceRigs(200000, 1000) == 1);
    }

    printf("\n=== relayout rebuilds pools and drops rigs ===\n");
    {
        World w;
        w.init(32, 8, 100, 1u);
        const uint32_t idx = w.pools.front().cells.front();
        check("rig placed before relayout",
              w.placeRig(cellX(w, idx), cellY(w, idx), 0, 1000) == true);
        w.resourceRate = 50;
        w.relayout();
        check("relayout clears rigs", w.rigs.empty());
        check("relayout rebuilds pools", !w.pools.empty());
    }

    printf("\n=== 512x512 generation stays fast enough ===\n");
    {
        const auto start = std::chrono::steady_clock::now();
        World w;
        w.init(512, 512, 6, 42u);
        const auto end = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(end - start).count();
        printf("  INFO  512x512 at 6%%: %d units, %d cells, %zu pools, %.2f ms\n",
               w.remainResource, countOreCells(w), w.pools.size(), ms);
        check("generation finishes well under a frame budget", ms < 100.0);
        check("default density is in the right ballpark",
              countOreCells(w) > 512 * 512 * 5 / 100 && countOreCells(w) < 512 * 512 * 7 / 100);
        check("all of those cells hold 100 units",
              w.remainResource == countOreCells(w) * ORE_PER_CELL);
    }

    printf("\nfailed checks: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
