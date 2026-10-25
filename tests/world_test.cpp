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

static int countNonEmpty(const World& w){
    int n = 0;
    for(const uint16_t cell : w.cells)
        if(cell != 0) n++;
    return n;
}

static int sumRemaining(const World& w){
    int sum = 0;
    for(const OrePool& pool : w.pools)
        sum += pool.remaining;
    return sum;
}

static void clearPool(World& w, OrePool& pool){
    for(const uint32_t idx : pool.cells){
        const int x = static_cast<int>(idx % static_cast<uint32_t>(w.width));
        const int y = static_cast<int>(idx / static_cast<uint32_t>(w.width));
        if(w.hasResource(x, y))
            w.collectAt(x, y);
    }
}

//池子里离矿机最近的一格。advanceRigs 的取矿顺序要和它一致。
static uint32_t nearestCell(const World& w, const OrePool& pool, int x, int y){
    uint32_t best = 0;
    int bestDistance = 1 << 30;
    for(const uint32_t idx : pool.cells){
        if(w.cells[idx] != pool.id) continue;
        const int cx = static_cast<int>(idx % static_cast<uint32_t>(w.width));
        const int cy = static_cast<int>(idx / static_cast<uint32_t>(w.width));
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
        const size_t idx = w.pools.front().cells.front();
        const int x = static_cast<int>(idx % static_cast<uint32_t>(w.width));
        const int y = static_cast<int>(idx / static_cast<uint32_t>(w.width));
        check("rig can be placed before the invalid calls", w.placeRig(x, y, 6000, 1000));

        const auto originalCells = w.cells;
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
        check("collectAt off the map is a no-op", w.collectAt(-3, 7) == false);
        check("placeRig off the map is a no-op", w.placeRig(-1, 2, 0, 100) == false);
    }

    printf("\n=== pools agree with the grid and the remaining count ===\n");
    {
        World w;
        w.init(64, 32, 25, 777u);
        check("pools exist at 25%%", !w.pools.empty());
        int summed = 0;
        bool idsOk = true;
        bool footprintsOk = true;
        bool sizesOk = true;
        for(size_t i = 0; i < w.pools.size(); i++){
            const OrePool& pool = w.pools[i];
            if(pool.id != static_cast<uint16_t>(i + 1)) idsOk = false;
            if(pool.capacity != static_cast<int>(pool.cells.size())) sizesOk = false;
            if(pool.remaining != countCellsOf(w, pool.id)) footprintsOk = false;
            if(pool.remaining > pool.capacity) sizesOk = false;
            for(const uint32_t idx : pool.cells)
                if(w.cells[idx] != pool.id) footprintsOk = false;
            summed += pool.remaining;
        }
        check("ids are 1..N in order", idsOk);
        check("capacity equals the footprint length", sizesOk);
        check("footprint covers exactly the pool's cells", footprintsOk);
        check("sum of pool remaining equals remainResource", summed == w.remainResource);
        check("remainResource equals the non-empty cell count", w.remainResource == countNonEmpty(w));
    }

    printf("\n=== collectAt keeps the pool bookkeeping in step ===\n");
    {
        World w;
        w.init(32, 8, 100, 1u);
        OrePool& pool = w.pools.front();
        const uint32_t idx = pool.cells.front();
        const int x = static_cast<int>(idx % static_cast<uint32_t>(w.width));
        const int y = static_cast<int>(idx / static_cast<uint32_t>(w.width));
        const int poolBefore = pool.remaining;
        const int remainBefore = w.remainResource;
        check("occupied cell returns true", w.collectAt(x, y) == true);
        check("cell cleared", w.hasResource(x, y) == false);
        check("remain decremented", w.remainResource == remainBefore - 1);
        check("pool remaining decremented", pool.remaining == poolBefore - 1);
        check("second pick returns false", w.collectAt(x, y) == false);
        check("empty cell leaves remain untouched", w.remainResource == remainBefore - 1);
    }

    printf("\n=== updatePools: one cell per pool per 5 minutes ===\n");
    {
        World w;
        w.init(32, 8, 100, 1u);
        check("regrow interval is 300s", ORE_REGROW_MS == 300ull * 1000ull);

        OrePool& pool = w.pools.front();
        const uint32_t idx = pool.cells.front();
        const int x = static_cast<int>(idx % static_cast<uint32_t>(w.width));
        const int y = static_cast<int>(idx / static_cast<uint32_t>(w.width));
        w.collectAt(x, y);
        const int afterMine = pool.remaining;

        const unsigned long long now = 10000;
        check("first update only starts the clock", w.updatePools(now) == 0);
        check("deadline is now + 5min", pool.nextRegrowAt == now + ORE_REGROW_MS);
        check("just before the deadline: nothing", w.updatePools(now + ORE_REGROW_MS - 1) == 0);
        check("pool still short", pool.remaining == afterMine);

        pool.nextRegrowAt = now;
        check("at the deadline: exactly one cell", w.updatePools(now) == 1);
        check("pool refilled by one", pool.remaining == afterMine + 1);
        check("the mined cell is back", w.hasResource(x, y) == true);
        check("next deadline rescheduled", pool.nextRegrowAt == now + ORE_REGROW_MS);

        check("a full pool does not grow", w.updatePools(now + ORE_REGROW_MS) == 0);
        check("full pool stays full", pool.remaining == pool.capacity);
    }

    printf("\n=== density still behaves statistically ===\n");
    {
        World w;
        w.init(64, 64, 100, 1u);
        check("rate=100 fills every cell", w.remainResource == 64 * 64);
        w.resourceRate = 0;
        w.relayout();
        check("rate=0 empties every cell", w.remainResource == 0);
        check("rate=0 leaves no pools", w.pools.empty());
        w.resourceRate = 30;
        w.relayout();
        double actual = w.remainResource * 100.0 / (w.width * w.height);
        printf("  INFO  rate=30%% measured %.2f%% (fixed seed, so this number is stable)\n", actual);
        check("rate=30% lands in a sane band", actual > 25.0 && actual < 35.0);
        int counted = 0;
        for(size_t i = 0; i < w.cells.size(); i++)
            if(w.cells[i] != 0)
                counted++;
        check("remain equals the cells actually set", counted == w.remainResource);
        check("and equals the sum of pool remaining", sumRemaining(w) == w.remainResource);
    }

    printf("\n=== placeRig: consumes the foundation cell, refuses invalid spots ===\n");
    {
        World w;
        w.init(40, 8, 100, 1u);
        OrePool& pool = w.pools.front();
        const uint32_t idx = pool.cells.front();
        const int x = static_cast<int>(idx % static_cast<uint32_t>(w.width));
        const int y = static_cast<int>(idx / static_cast<uint32_t>(w.width));
        const int remainBefore = w.remainResource;
        const int poolBefore = pool.remaining;
        const unsigned long long now = 1000;

        check("placement on an ore cell succeeds", w.placeRig(x, y, now, 1000) == true);
        check("foundation cell is eaten", w.hasResource(x, y) == false);
        check("remain decremented by the foundation", w.remainResource == remainBefore - 1);
        check("pool remaining follows", pool.remaining == poolBefore - 1);
        check("one rig stored", w.rigs.size() == 1);
        check("rig remembers its pool", w.rigs[0].pool == pool.id);
        check("rig first step is one interval away", w.rigs[0].nextAt == now + 1000);
        check("the same cell cannot host a second rig", w.placeRig(x, y, now, 1000) == false);
        check("zero step is rejected", w.placeRig(x, y, now, 0) == false);

        World empty;
        empty.init(32, 8, 0, 1u);
        check("placement on empty ground fails", empty.placeRig(0, 0, 0, 1000) == false);
    }

    printf("\n=== advanceRigs: nearest cell, timing gate, idling ===\n");
    {
        World w;
        w.init(40, 8, 100, 1u);
        OrePool& pool = w.pools.front();
        const uint32_t idx = pool.cells.front();
        const int x = static_cast<int>(idx % static_cast<uint32_t>(w.width));
        const int y = static_cast<int>(idx / static_cast<uint32_t>(w.width));
        check("rig placed", w.placeRig(x, y, 1000, 1000));
        Rig& rig = w.rigs[0];

        const uint32_t expected = nearestCell(w, pool, rig.x, rig.y);
        const int poolBefore = pool.remaining;
        const int remainBefore = w.remainResource;

        check("before the deadline: nothing", w.advanceRigs(1999, 1000) == 0);
        check("at the deadline: one cell", w.advanceRigs(2000, 1000) == 1);
        check("the nearest cell is the one taken", w.cells[expected] == 0);
        check("pool remaining decremented", pool.remaining == poolBefore - 1);
        check("remain decremented", w.remainResource == remainBefore - 1);
        check("next step rescheduled from now", rig.nextAt == 3000);

        clearPool(w, pool);
        check("pool drained", pool.remaining == 0);
        rig.nextAt = 10000;
        check("idle rig mines nothing", w.advanceRigs(10000, 1000) == 0);
        check("idle rig still reschedules", rig.nextAt == 11000);

        //池子回一格之后,矿机又能接着挖。
        pool.nextRegrowAt = 20000;
        check("one cell regrown", w.updatePools(20000) == 1);
        rig.nextAt = 20000;
        check("rig picks the regrown cell", w.advanceRigs(20000, 1000) == 1);
    }

    printf("\n=== relayout rebuilds pools and drops rigs ===\n");
    {
        World w;
        w.init(32, 8, 100, 1u);
        const uint32_t idx = w.pools.front().cells.front();
        check("rig placed before relayout",
              w.placeRig(static_cast<int>(idx % static_cast<uint32_t>(w.width)),
                         static_cast<int>(idx / static_cast<uint32_t>(w.width)), 0, 1000));
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
        printf("  INFO  512x512 at 6%%: %d cells, %zu pools, %.2f ms\n",
               w.remainResource, w.pools.size(), ms);
        check("generation finishes well under a frame budget", ms < 100.0);
        check("default density is in the right ballpark",
              w.remainResource > 512 * 512 * 5 / 100 && w.remainResource < 512 * 512 * 7 / 100);
    }

    printf("\nfailed checks: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
