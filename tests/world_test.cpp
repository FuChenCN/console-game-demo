//针对真实 World 实现的行为测试:直接链接 src/game/world.cpp,不开终端、不画屏幕。
#include "game/world.h"

#include <cstdio>
#include <stdexcept>

static int failures = 0;
static void check(const char* what, bool ok){
    printf("  %s  %s\n", ok ? "PASS" : "FAIL", what);
    if(!ok) failures++;
}

//测试需要摆出特定布局。cells 是一维的、行优先,而且 remainResource 必须与它保持一致
//(这条不变量写在 World 的注释里),所以改动只能走这里,不在用例里散着写。
static void setOre(World& w, int x, int y, bool has){
    size_t idx = static_cast<size_t>(y) * static_cast<size_t>(w.width) + static_cast<size_t>(x);
    uint16_t want = has ? 1 : 0;
    if(w.cells[idx] == want)
        return;
    w.cells[idx] = want;
    w.remainResource += has ? 1 : -1;
}

int main(){
    printf("=== invalid dimensions are rejected without changing the world ===\n");
    {
        World w;
        w.init(8, 4, 100);
        w.syncBots(1);
        w.botDeadline = 1234;
        const auto originalCells = w.cells;
        const Bot originalBot = w.bots.front();
        const int invalidSizes[][2] = {{0, 4}, {8, 0}, {-1, 4}, {8, -1}};
        for(const auto& size : invalidSizes){
            printf("  input: %d x %d\n", size[0], size[1]);
            bool rejected = false;
            try{
                w.init(size[0], size[1], 0);
            }
            catch(const std::invalid_argument&){
                rejected = true;
            }
            check("nonpositive size throws invalid_argument", rejected);
            check("dimensions and grid stay unchanged",
                  w.width == 8 && w.height == 4 && w.cells == originalCells);
            check("resource bookkeeping stays unchanged",
                  w.resourceRate == 100 && w.remainResource == 32);
            check("bots and deadline stay unchanged",
                  w.botDeadline == 1234 && w.bots.size() == 1 &&
                  w.bots[0].x == originalBot.x && w.bots[0].y == originalBot.y &&
                  w.bots[0].dir == originalBot.dir && w.bots[0].justAte == originalBot.justAte);
        }
    }

    printf("=== storage is on the heap and sized at runtime ===\n");
    {
        World w;
        w.init(64, 16, 0);
        check("object itself stays small", sizeof(World) < 256);
        check("cells sized width*height", w.cells.size() == 64 * 16);
        check("runtime size honored", w.width == 64 && w.height == 16);
        printf("  INFO  sizeof(World) = %zu bytes regardless of map size\n", sizeof(World));

        World big;
        big.init(512, 512, 0);
        check("same code handles 512x512", big.cells.size() == 512u * 512u);
        check("grow does not change object size", sizeof(World) == sizeof(big));
        //init 会清空机器人:新世界不该带着旧世界的机器。
        big.syncBots(2);
        big.init(64, 64, 0);
        check("init clears bots from the previous world", big.bots.empty());
    }

    printf("\n=== bounds are safe ===\n");
    {
        World w;
        w.init(8, 4, 0);
        check("inBounds rejects negative", w.inBounds(-1, 0) == false);
        check("inBounds rejects far x", w.inBounds(8, 0) == false);
        check("inBounds rejects far y", w.inBounds(0, 4) == false);
        check("inBounds accepts the corner", w.inBounds(7, 3) == true);
        check("hasResource off the map returns false, no crash", w.hasResource(9999, 9999) == false);
        check("collectAt off the map is a no-op", w.collectAt(-3, 7) == false);
    }

    printf("\n=== collectAt ===\n");
    {
        World w;
        w.init(32, 8, 0);
        setOre(w, 5, 2, true);
        check("empty cell returns false", w.collectAt(6, 2) == false);
        check("empty cell leaves remain untouched", w.remainResource == 1);
        check("occupied cell returns true", w.collectAt(5, 2) == true);
        check("cell cleared", w.hasResource(5, 2) == false);
        check("remain decremented", w.remainResource == 0);
        check("second pick returns false", w.collectAt(5, 2) == false);
    }

    printf("\n=== syncBots: one bot per row, first row is y=0 ===\n");
    {
        World w;
        w.init(40, 8, 0);
        w.syncBots(3);
        check("three bots created", w.bots.size() == 3);
        check("first bot starts at x=1, y=0", w.bots[0].x == 1 && w.bots[0].y == 0);
        check("second bot on row 1", w.bots[1].y == 1);
        check("all start facing right", w.bots[0].dir == 1 && w.bots[2].dir == 1);
        w.syncBots(2);
        check("shrinking owned does not remove bots", w.bots.size() == 3);
        w.syncBots(w.height);
        bool everyRow = true;
        for(int row = 0; row < w.height; row++)
            if(w.bots[row].y != row)
                everyRow = false;
        check("rows 0..height-1 each used exactly once", w.bots.size() == static_cast<size_t>(w.height) && everyRow);
        //行数由运行期尺寸决定:同样的代码在 40x8 的图上限就是 8,不是编译期的 32。
        check("row limit follows runtime height", w.height == 8);
    }

    printf("\n=== advanceBots: timing gate ===\n");
    {
        World w;
        w.init(40, 8, 0);
        w.syncBots(1);
        w.botDeadline = 1000;
        BotStep early = w.advanceBots(999, 1000);
        check("before deadline: no move", early.moved == false && early.collected == 0);
        check("bot position unchanged", w.bots[0].x == 1);
        BotStep onTime = w.advanceBots(1000, 1000);
        check("at deadline: moved", onTime.moved == true);
        check("stepped exactly one cell", w.bots[0].x == 2);
        check("next deadline rescheduled from now", w.botDeadline == 2000);
    }

    printf("\n=== advanceBots: eat only the cell it arrives at ===\n");
    {
        World w;
        w.init(40, 8, 0);
        w.syncBots(1);
        w.botDeadline = 0;
        setOre(w, 2, 0, true);      //bot 在 x=1,下一步会踩到这格
        setOre(w, 9, 0, true);      //同一行更远处
        check("remain seeded to 2", w.remainResource == 2);
        BotStep s1 = w.advanceBots(1000, 1000);
        check("arrived cell had resource -> collected 1", s1.collected == 1);
        check("bot shows the bite frame", w.bots[0].justAte == true);
        check("resource cleared at that cell", w.hasResource(2, 0) == false);
        check("far resource untouched", w.hasResource(9, 0) == true);
        BotStep s2 = w.advanceBots(2000, 1000);
        check("arrived at empty cell -> collected 0", s2.collected == 0);
        check("glyph falls back to open mouth", w.bots[0].justAte == false);
        check("remain tracked the bot pickup", w.remainResource == 1);
    }

    printf("\n=== advanceBots: wall turnaround uses runtime width ===\n");
    {
        World w;
        w.init(40, 8, 0);
        w.syncBots(1);
        w.botDeadline = 0;
        w.bots[0].x = w.width - 2;
        BotStep hit = w.advanceBots(1000, 1000);
        check("lands exactly on the last column", w.bots[0].x == w.width - 1);
        check("flips to face left", w.bots[0].dir == -1 && hit.moved == true);
        w.advanceBots(2000, 1000);
        check("moves back inward", w.bots[0].x == w.width - 2);
        w.bots[0].x = 1;
        w.bots[0].dir = -1;
        w.advanceBots(3000, 1000);
        check("hits left wall and clamps at 0", w.bots[0].x == 0);
        check("faces right again", w.bots[0].dir == 1);
    }

    printf("\n=== refillIfSparse ===\n");
    {
        World w;
        w.resourceRate = 100;
        w.init(32, 8, 100);                       //填满,remain = 256 > 50
        w.refillIfSparse();
        check("above threshold: nothing refilled", w.remainResource == w.width * w.height);

        World sparse;
        sparse.init(32, 8, 0);                    //全空,remain = 0
        sparse.resourceRate = 100;
        sparse.refillIfSparse();
        check("at/below threshold with rate=100: whole map filled", sparse.remainResource == sparse.width * sparse.height);

        World zero;
        zero.init(32, 8, 0);
        zero.refillIfSparse();
        check("rate=0 scans the map but adds nothing", zero.remainResource == 0);

        //阈值是绝对值 50,不随地图缩放:这是矿脉方案要替换掉的东西,先固化成事实。
        World wide;
        wide.init(512, 512, 0);
        check("threshold 50 is 0.019% of a 512x512 map (why refills stop firing)", wide.remainResource <= 50);
    }

    printf("\n=== relayout density sanity ===\n");
    {
        World w;
        w.init(64, 64, 100);
        check("rate=100 fills every cell", w.remainResource == 64 * 64);
        w.resourceRate = 0;
        w.relayout();
        check("rate=0 empties every cell", w.remainResource == 0);
        w.resourceRate = 30;
        w.relayout();
        double actual = w.remainResource * 100.0 / (w.width * w.height);
        printf("  INFO  rate=30%% measured %.1f%% (rand, no fixed seed)\n", actual);
        check("rate=30% lands in a sane band", actual > 20.0 && actual < 40.0);
        int counted = 0;
        for(size_t i = 0; i < w.cells.size(); i++)
            if(w.cells[i] != 0)
                counted++;
        check("remain equals the cells actually set", counted == w.remainResource);
    }

    printf("\nfailed checks: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
