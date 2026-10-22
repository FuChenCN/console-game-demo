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
        w.syncBots(1);
        w.init(48, 24, 8, 7u);
        check("init clears bots", w.bots.empty());

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
        int far = 0;
        for(int row = d.minY; row <= d.maxY; row++)
            for(int col = d.minX; col <= d.maxX; col++)
                if(w.depositAt(col, row) == id && w.showsOre(col, row))
                    far++;
        check("one probe reveals the whole vein, not just the 3x3", far == d.cellCount);

        World off;
        off.init(16, 16, 8, 3u);
        check("probing off the map is safe", off.probeAt(-50, -50) == 0);
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

    printf("\n=== hand mining and bots draw from the same pool ===\n");
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

        World botWorld;
        botWorld.init(64, 8, 30, 11u);              //高覆盖率保证机器人那条行上有矿
        int bx = 0, by = 0, bid = 0;
        bool ore = false;
        for(int row = 0; row < botWorld.height && !ore; row++)
            for(int col = 0; col < botWorld.width && !ore; col++){
                int found = botWorld.depositAt(col, row);
                if(found != NO_DEPOSIT){ bx = col; by = row; bid = found; ore = true; }
            }
        check("found ore for the bot test", ore);
        botWorld.probeAt(bx, by);
        botWorld.syncBots(1);
        //机器人是"先走一格、再判定脚下",所以要摆在目标格的前一格。
        //目标恰好在最左列时,改成从右边接近,免得撞上左墙分支。
        botWorld.bots[0].y = by;
        if(bx > 0){
            botWorld.bots[0].x = bx - 1;
            botWorld.bots[0].dir = 1;
        }
        else{
            botWorld.bots[0].x = bx + 1;
            botWorld.bots[0].dir = -1;
        }
        botWorld.botDeadline = 0;
        const int before = botWorld.findDeposit(bid)->remaining;
        BotStep step = botWorld.advanceBots(1000, 1000);
        check("a bot that steps onto ore reports it", step.moved && botWorld.bots[0].justAte == true);
        check("the bot takes exactly BOT_TAKE from the pool",
              before - botWorld.findDeposit(bid)->remaining == BOT_TAKE);
        check("collected count matches", step.collected == 1);
    }

    printf("\n=== timing gate and walls still use runtime size ===\n");
    {
        World w;
        w.init(40, 8, 0, 1u);
        w.syncBots(1);
        w.botDeadline = 1000;
        BotStep early = w.advanceBots(999, 1000);
        check("before deadline: no move", early.moved == false && w.bots[0].x == 1);
        BotStep onTime = w.advanceBots(1000, 1000);
        check("at deadline: one cell", onTime.moved && w.bots[0].x == 2);
        check("next deadline rescheduled", w.botDeadline == 2000);

        w.bots[0].x = w.width - 2;
        w.advanceBots(2000, 1000);
        check("clamps at the right wall", w.bots[0].x == w.width - 1);
        check("faces left", w.bots[0].dir == -1);
        w.bots[0].x = 1;
        w.bots[0].dir = -1;
        w.advanceBots(3000, 1000);
        check("clamps at the left wall", w.bots[0].x == 0);
        check("faces right again", w.bots[0].dir == 1);

        w.syncBots(w.height);
        bool rows = true;
        for(int row = 0; row < w.height; row++)
            if(w.bots[row].y != row) rows = false;
        check("one bot per row starting at y=0", w.bots.size() >= static_cast<size_t>(w.height) && rows);
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
