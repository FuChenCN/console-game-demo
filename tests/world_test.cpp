//针对真实 World 实现的行为测试:直接链接 src/game/world.cpp,不开终端、不画屏幕。
#include "game/world.h"

#include <cstddef>     //size_t
#include <cstdio>
#include <stdexcept>   //catch 用 std::invalid_argument,必须自己包含
#include <string>

static int failures = 0;
static void check(const char* what, bool ok){
    printf("  %s  %s\n", ok ? "PASS" : "FAIL", what);
    if(!ok) failures++;
}

//在真实生成的图里找一条脉的任意一格,测试不手工摆格子:
//池模型下"格子只存归属 id",手摆出来的脉没有 Deposit 条目,测不到真实路径。
static bool anyOreCell(const World& w, int& x, int& y, int& id){
    for(int row = 0; row < w.height; row++){
        for(int col = 0; col < w.width; col++){
            int found = w.depositAt(col, row);
            if(found != NO_DEPOSIT){
                x = col; y = row; id = found;
                return true;
            }
        }
    }
    return false;
}

static int oreCellCount(const World& w){
    int n = 0;
    for(size_t i = 0; i < w.cells.size(); i++)
        if(w.cells[i] != NO_DEPOSIT)
            n++;
    return n;
}

//还剩下多少格属于这条脉:枯竭时应当整条撤走,这里就是用来确认"一根不剩"的。
static int oreCellCountOfId(const World& w, int id){
    int n = 0;
    for(size_t i = 0; i < w.cells.size(); i++)
        if(w.cells[i] == static_cast<uint16_t>(id))
            n++;
    return n;
}

//这条脉当前还画得出多少个 #:池子每少 100# 就该少一格。
static int visibleCellCountOfId(const World& w, int id){
    int n = 0;
    for(int row = 0; row < w.height; row++)
        for(int col = 0; col < w.width; col++)
            if(w.depositAt(col, row) == id && w.showsOre(col, row))
                n++;
    return n;
}

//这一格是否在原脉的外缘:贴地图边,或有一个四邻不属于这条脉。
static bool onVeinRim(const World& w, int id, int x, int y){
    const int dx[4] = {1, -1, 0, 0};
    const int dy[4] = {0, 0, 1, -1};
    for(int k = 0; k < 4; k++){
        int nx = x + dx[k];
        int ny = y + dy[k];
        if(nx < 0 || nx >= w.width || ny < 0 || ny >= w.height)
            return true;
        if(w.depositAt(nx, ny) != id)
            return true;
    }
    return false;
}

int main(){
    printf("=== storage and runtime size ===\n");
    {
        World w;
        w.init(48, 24, 8, 7u);
        check("object stays small, grid lives on the heap", sizeof(World) < 512);
        check("cells sized width*height", w.cells.size() == 48 * 24);
        check("runtime size honored", w.width == 48 && w.height == 24);
        check("nothing discovered at start",
              [&]{ for(const Deposit& d : w.deposits) if(d.discovered) return false; return true; }());
        w.tryPlaceMiner(1, 1);
        w.init(48, 24, 8, 7u);
        check("init clears miners and their footprints", w.miners.empty() && w.machineCells.size() == 48 * 24);
        bool footprintClean = true;
        for(size_t i = 0; i < w.machineCells.size(); i++)
            if(w.machineCells[i] != 0)
                footprintClean = false;
        check("init wipes every machine cell", footprintClean);

        World big;
        big.init(512, 512, 8, 7u);
        check("512x512 allocates and generates", big.cells.size() == 512u * 512u && !big.deposits.empty());
        check("ids fit in the 16 bit grid", big.deposits.size() < 65534);
    }

    printf("\n=== invalid sizes are rejected by World's own contract ===\n");
    {
        World w;
        w.init(48, 24, 8, 7u);
        const auto cells = w.cells;
        bool threw = false;
        try{ w.init(0, 24, 8, 7u); } catch(const std::invalid_argument&){ threw = true; }
        check("zero width throws", threw);
        check("grid untouched by the rejected call", w.cells == cells);
        threw = false;
        try{ w.init(48, -1, 8, 7u); } catch(const std::invalid_argument&){ threw = true; }
        check("negative height throws", threw);
    }

    printf("\n=== generator shape ===\n");
    {
        World w;
        w.init(128, 128, 8, 42u);                 //16384 格,8% 目标约 1310 格
        double actual = oreCellCount(w) * 100.0 / (128 * 128);
        printf("  INFO  coverage measured %.2f%% over %d veins\n", actual, (int)w.deposits.size());
        check("coverage lands near the requested 8%", actual > 5.0 && actual < 11.0);
        check("more than one vein on a 128x128 map", w.deposits.size() > 1);

        bool sizesOk = true, idsOk = true, totalsOk = true;
        for(size_t i = 0; i < w.deposits.size(); i++){
            const Deposit& d = w.deposits[i];
            if(d.cellCount < DEPOSIT_MIN_CELLS || d.cellCount > DEPOSIT_MAX_CELLS)
                sizesOk = false;
            if(d.id != static_cast<int>(i) + 1)
                idsOk = false;                      //id 必须连续且从 1 起:格子存的就是它
            if(d.total != d.cellCount * ORE_PER_CELL || d.remaining != d.total)
                totalsOk = false;
        }
        check("every vein is within [32,128] cells", sizesOk);
        check("ids are 1..n with no gaps", idsOk);
        check("total = area x 100#, remaining starts full", totalsOk);

        //玩家出生在地图中心,首条脉必须盖住那里,否则开局无矿可采。
        check("first vein is seeded at the map centre",
              [&]{
                  int id = w.depositAt(w.width / 2, w.height / 2);
                  return id == 1 && w.findDeposit(id) != nullptr;
              }());

        World bare;
        bare.init(64, 64, 0, 5u);
        check("coverage 0 produces no veins", bare.deposits.empty() && oreCellCount(bare) == 0);
    }

    printf("\n=== discovery gates both rendering and mining ===\n");
    {
        World w;
        w.init(128, 128, 8, 42u);
        int x = 0, y = 0, id = 0;
        anyOreCell(w, x, y, id);
        check("an undiscovered vein does not draw as ore", w.showsOre(x, y) == false);
        check("and cannot be mined", w.takeFrom(x, y, 5) == 0);
        check("its pool is intact after a refused mine", w.findDeposit(id)->remaining == w.findDeposit(id)->total);

        int lit = w.probeAt(x, y);
        check("probing reports the vein it revealed", lit == 1);
        check("after probing it draws", w.showsOre(x, y) == true);
        check("and can be mined", w.takeFrom(x, y, 1) == 1);
        check("probing the same spot again reveals nothing new", w.probeAt(x, y) == 0);

        //点亮的是整条脉,不是被碰到的那几格:格子只存归属,所以一次翻转即见全貌。
        const Deposit& d = *w.findDeposit(id);
        int litCells = 0;                       //注意别用 far/near 当变量名:windef.h 把它们定义成空宏
        for(int row = d.minY; row <= d.maxY; row++)
            for(int col = d.minX; col <= d.maxX; col++)
                if(w.depositAt(col, row) == id && w.showsOre(col, row))
                    litCells++;
        check("one probe reveals the whole vein, not just the 3x3", litCells == d.cellCount);

        World off;
        off.init(16, 16, 8, 3u);
        check("probing off the map is safe", off.probeAt(-50, -50) == 0);
    }

    printf("\n=== mining erodes the vein from the rim ===\n");
    {
        World w;
        w.init(128, 128, 8, 42u);
        int x = 0, y = 0, id = 0;
        anyOreCell(w, x, y, id);
        w.probeAt(x, y);
        Deposit& d = *w.findDeposit(id);
        const int total = d.total;

        check("a full vein draws every cell", visibleCellCountOfId(w, id) == d.cellCount);

        d.remaining = total - 100;
        check("each full 100# hides exactly one #", visibleCellCountOfId(w, id) == d.cellCount - 1);
        d.remaining = total - 199;
        check("a partial hundred hides nothing extra", visibleCellCountOfId(w, id) == d.cellCount - 1);
        d.remaining = total - 200;
        check("two full hundreds hide two cells", visibleCellCountOfId(w, id) == d.cellCount - 2);

        d.remaining = total - 100;
        int hiddenX = -1, hiddenY = -1, hidden = 0;
        for(int row = 0; row < w.height; row++)
            for(int col = 0; col < w.width; col++)
                if(w.depositAt(col, row) == id && !w.showsOre(col, row)){
                    hiddenX = col;
                    hiddenY = row;
                    hidden++;
                }
        check("exactly one cell is hidden after 100#", hidden == 1);
        check("the first cell to vanish is on the rim",
              hidden == 1 && onVeinRim(w, id, hiddenX, hiddenY));

        d.remaining = 1;
        check("the last unit leaves exactly one #", visibleCellCountOfId(w, id) == 1);

        //同 seed 的剥落顺序必须可复现,不能带随机设备状态。
        World twin;
        twin.init(128, 128, 8, 42u);
        twin.probeAt(x, y);                       //不探测的话整条脉不会画出来,比较没有意义
        twin.findDeposit(id)->remaining = total - 300;
        d.remaining = total - 300;
        bool samePattern = true;
        for(int row = 0; row < w.height && samePattern; row++)
            for(int col = 0; col < w.width && samePattern; col++)
                if(w.depositAt(col, row) == id &&
                   w.showsOre(col, row) != twin.showsOre(col, row))
                    samePattern = false;
        check("the same seed erodes the same cells", samePattern);
    }

    printf("\n=== the pool is the single source of truth ===\n");
    {
        World w;
        w.init(128, 128, 8, 42u);
        int x = 0, y = 0, id = 0;
        anyOreCell(w, x, y, id);
        w.probeAt(x, y);
        Deposit& d = *w.findDeposit(id);
        const int total = d.total;

        check("take shrinks the pool", w.takeFrom(x, y, 100) == 100 && d.remaining == total - 100);
        check("overdrawing only gives what is left", [&]{
            w.findDeposit(id)->remaining = 3;
            return w.takeFrom(x, y, 50) == 3;
        }());
        check("emptying the vein exhausts it", d.exhausted == true && d.remaining == 0);
        check("exhausted vein disappears from the whole map", oreCellCountOfId(w, id) == 0);
        check("and it no longer draws", w.showsOre(x, y) == false);
        check("mining an exhausted vein yields nothing", w.takeFrom(x, y, 5) == 0);
        check("the id survives, so other cells still resolve", w.findDeposit(id) != nullptr);

        World sum;
        sum.init(128, 128, 8, 9u);
        int tally = 0;
        for(const Deposit& each : sum.deposits)
            if(!each.exhausted)
                tally += each.remaining;
        check("remainingUnits equals the sum over veins", sum.remainingUnits() == tally);
        check("remainingUnits is not a cell count any more", sum.remainingUnits() > oreCellCount(sum));
    }

    printf("\n=== hand mining and miners draw from the same pool ===\n");
    {
        World w;
        w.init(128, 128, 8, 42u);
        int x = 0, y = 0, id = 0;
        anyOreCell(w, x, y, id);
        w.probeAt(x, y);
        Deposit& d = *w.findDeposit(id);

        bool inRange = true;
        for(int i = 0; i < 200; i++){
            int got = w.playerTake(x, y);
            if(got < PLAYER_TAKE_MIN || got > PLAYER_TAKE_MAX)
                inRange = false;
        }
        check("playerTake always lands in [1,3] while the pool lasts", inRange);

        d.remaining = 2;
        check("a nearly empty vein caps the player's take", w.playerTake(x, y) <= 2);
        check("and the pool never goes negative", d.remaining >= 0);

        //矿机:产出留在缓存里,不进玩家背包——这是"得有人来取"这条玩法的前提。
        World mw;
        mw.init(64, 8, 30, 11u);              //覆盖率高,总有能塞下 3x2 的脉
        int mx = 0, my = 0, mid = 0, veinId = 0;
        bool foundSpot = false;
        for(int row = 0; row + MINER_H <= mw.height && !foundSpot; row++){
            for(int col = 0; col + MINER_W <= mw.width && !foundSpot; col++){
                mw.probeAt(col, row);                              //探一下,让脚下这条脉可见
                PlaceOutcome put = mw.tryPlaceMiner(col, row);
                if(put.result == PlaceResult::placed){
                    mx = col; my = row;
                    mid = put.minerId;
                    veinId = mw.depositAt(col, row);   //矿机 id 和矿脉 id 是两套序号,不能混用
                    foundSpot = true;
                }
            }
        }
        check("found a legal spot to place a miner", foundSpot);
        if(foundSpot){
            bool footprint = true;
            for(int r = 0; r < MINER_H; r++)
                for(int c = 0; c < MINER_W; c++)
                    if(mw.machineCells[static_cast<size_t>(my + r) * mw.width + mx + c] != mid)
                        footprint = false;
            check("all six cells point at the miner", footprint);
            check("and the machine reports itself", mw.minerAt(mx, my) != nullptr);
            check("placing the same spot again is refused",
                  mw.tryPlaceMiner(mx, my).result == PlaceResult::blocked);
            check("a spot overlapping it is refused too",
                  mw.tryPlaceMiner(mx + 1, my).result == PlaceResult::blocked);

            Deposit* vein = mw.findDeposit(veinId);
            check("the miner sits on a real vein", vein != nullptr);
            if(vein != nullptr){
                const int poolBefore = vein->remaining;
                mw.minerDeadline = 0;
                check("one tick moves ore from the pool into the cache", mw.advanceMiners(1000, 1000));
                Miner* m = mw.minerAt(mx, my);
                check("cache holds exactly MINER_TAKE", m != nullptr && m->buffer == MINER_TAKE);
                check("the vein lost exactly what the cache gained",
                      poolBefore - vein->remaining == MINER_TAKE);

                int drained = 0;
                while(m->buffer < MINER_BUFFER && drained++ < MINER_BUFFER + 5)
                    mw.advanceMiners(2000ULL + drained * 1000ULL, 1000);
                check("the cache fills up to its cap and not beyond", m->buffer == MINER_BUFFER);
                check("a full machine goes idle", m->active == false);
                const int whileIdle = vein->remaining;
                mw.advanceMiners(90000, 1000);
                check("an idle machine stops drawing from the vein", vein->remaining == whileIdle);

                check("retrieving hands over the whole cache", mw.retrieveAt(mx, my) == MINER_BUFFER);
                check("cache is empty afterwards", m->buffer == 0);
                check("and it resumes working", m->active == true);
                check("retrieving an empty cache gives nothing", mw.retrieveAt(mx, my) == 0);

                //脉还活着时拆一次:占地立刻腾空,槽位和 id 应被下一次放置复用。
                check("removing a working miner succeeds", mw.removeMinerAt(mx, my));
                check("the footprint is freed", mw.minerAt(mx, my) == nullptr);
                PlaceOutcome again = mw.tryPlaceMiner(mx, my);
                check("and the slot is reused, keeping ids stable",
                      again.result == PlaceResult::placed && again.minerId == mid);
                Miner* replacement = mw.minerAt(mx, my);
                check("the replacement starts empty", replacement != nullptr && replacement->buffer == 0);

                //采空整条脉:机器该停下来,并且是被识别为"没料"而不是崩掉。
                vein->remaining = 1;
                mw.advanceMiners(100000, 1000);
                mw.advanceMiners(101000, 1000);
                check("the vein runs dry", vein->exhausted == true);
                check("the miner keeps standing there but goes idle",
                      replacement != nullptr && replacement->active == false);
                const int cached = replacement != nullptr ? replacement->buffer : 0;
                check("the idle machine still holds what it mined", cached > 0);
                check("removing it works even on an exhausted vein", mw.removeMinerAt(mx, my));
                check("an exhausted footprint cannot be re-used",
                      mw.tryPlaceMiner(mx, my).result == PlaceResult::no_vein);
            }
        }
    }

    printf("\n=== timing gate and placement guards ===\n");
    {
        World w;
        w.init(64, 8, 30, 3u);
        w.minerDeadline = 5000;
        check("before the deadline nothing advances", w.advanceMiners(4999, 1000) == false);
        check("and the deadline is left alone", w.minerDeadline == 5000);

        //越界、无脉、跨脉三种放不下的情况都要能区分开,提示才写得出人话。
        check("placement off the map is refused", w.tryPlaceMiner(-1, 0).result == PlaceResult::out_of_bounds);
        check("placement at the far edge is refused",
              w.tryPlaceMiner(w.width - 1, 0).result == PlaceResult::out_of_bounds);
        check("plain ground refuses", w.tryPlaceMiner(0, 0).result == PlaceResult::no_vein ||
                                    w.tryPlaceMiner(0, 0).result == PlaceResult::undiscovered);

        //未探明的脉即使几何合适也不能放。
        int hiddenX = -1, hiddenY = -1, hiddenId = 0;
        for(int row = 0; row + MINER_H <= w.height && hiddenX < 0; row++)
            for(int col = 0; col + MINER_W <= w.width && hiddenX < 0; col++){
                int id = w.depositAt(col, row);
                if(id != NO_DEPOSIT && !w.findDeposit(id)->discovered){
                    bool sameVein = true;
                    for(int r = 0; r < MINER_H && sameVein; r++)
                        for(int c = 0; c < MINER_W && sameVein; c++)
                            if(w.depositAt(col + c, row + r) != id) sameVein = false;
                    if(sameVein){ hiddenX = col; hiddenY = row; hiddenId = id; }
                }
            }
        if(hiddenX >= 0){
            check("an undiscovered vein blocks placement",
                  w.tryPlaceMiner(hiddenX, hiddenY).result == PlaceResult::undiscovered);
            w.probeAt(hiddenX, hiddenY);
            check("probing it makes placement legal",
                  w.tryPlaceMiner(hiddenX, hiddenY).result == PlaceResult::placed);
            check("the vein was revealed as a whole", w.findDeposit(hiddenId)->discovered == true);
        }
    }

    printf("\n=== same seed replays the same world ===\n");
    {
        World a, b, other;
        a.init(128, 128, 8, 12345u);
        b.init(128, 128, 8, 12345u);
        other.init(128, 128, 8, 99999u);
        check("same seed, same veins", a.cells == b.cells && a.deposits.size() == b.deposits.size());
        check("same seed, same totals", [&]{
            for(size_t i = 0; i < a.deposits.size(); i++)
                if(a.deposits[i].total != b.deposits[i].total || a.deposits[i].cellCount != b.deposits[i].cellCount)
                    return false;
            return true;
        }());
        check("a different seed, a different map", a.cells != other.cells);

        a.regenerate();
        check("regenerate replays from the seed", a.cells == b.cells);
        a.coverage = 40;
        a.regenerate();
        check("raising coverage keeps old cells as a subset", [&]{
            for(size_t i = 0; i < b.cells.size(); i++)
                if(b.cells[i] != NO_DEPOSIT && a.cells[i] == NO_DEPOSIT)
                    return false;                                   //同序列、门槛更宽 → 只多不少
            return true;
        }());
        check("and really adds more ore", oreCellCount(a) > oreCellCount(b));
    }

    printf("\nfailed checks: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
