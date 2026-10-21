//对真实 World 实现的单元测试:直接链接 src/game/world.cpp,不开终端、不画屏幕。
#include "game/world.h"

#include <cstdio>

static int failures = 0;
static void check(const char* what, bool ok){
    printf("  %s  %s\n", ok ? "PASS" : "FAIL", what);
    if(!ok) failures++;
}

int main(){
    printf("=== collectAt ===\n");
    {
        World w;
        w.resource[7][3] = true;
        w.remainResource = 1;
        check("empty cell returns false", w.collectAt(0, 0) == false);
        check("empty cell leaves remain untouched", w.remainResource == 1);
        check("occupied cell returns true", w.collectAt(3, 7) == true);
        check("cell cleared", w.resource[7][3] == false);
        check("remain decremented", w.remainResource == 0);
        check("second pick on same cell returns false", w.collectAt(3, 7) == false);
        check("remain not driven negative by double pick", w.remainResource == 0);
    }

    printf("\n=== syncBots: one bot per row, first row is y=0 ===\n");
    {
        World w;
        w.syncBots(3);
        check("three bots created", w.bots.size() == 3);
        check("first bot starts at x=1, y=0", w.bots[0].x == 1 && w.bots[0].y == 0);
        check("second bot on row 1", w.bots[1].y == 1);
        check("third bot on row 2", w.bots[2].y == 2);
        check("all start facing right", w.bots[0].dir == 1 && w.bots[2].dir == 1);
        w.syncBots(2);
        check("shrinking owned does not remove bots", w.bots.size() == 3);
        w.syncBots(MAP_H);
        check("filled every row exactly once", w.bots.size() == MAP_H && w.bots[MAP_H - 1].y == MAP_H - 1);
    }

    printf("\n=== advanceBots: timing gate ===\n");
    {
        World w;
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
        w.syncBots(1);
        w.botDeadline = 0;
        //bot 现在 x=1。资源放在 x=2(下一步会到)和 x=9(远着)。
        w.resource[0][2] = true;
        w.resource[0][9] = true;
        w.remainResource = 2;
        BotStep s1 = w.advanceBots(1000, 1000);
        check("arrived cell had resource -> collected 1", s1.collected == 1);
        check("bot shows the bite frame", w.bots[0].justAte == true);
        check("resource cleared at that cell", w.resource[0][2] == false);
        check("far resource untouched", w.resource[0][9] == true);
        BotStep s2 = w.advanceBots(2000, 1000);
        check("arrived at empty cell -> collected 0", s2.collected == 0);
        check("glyph falls back to open mouth", w.bots[0].justAte == false);
        check("remain tracked the bot pickup", w.remainResource == 1);
    }

    printf("\n=== advanceBots: wall turnaround ===\n");
    {
        World w;
        w.syncBots(1);
        w.botDeadline = 0;
        w.bots[0].x = MAP_W - 2;          //126
        BotStep hit = w.advanceBots(1000, 1000);
        check("lands exactly on last column", w.bots[0].x == MAP_W - 1);
        check("flips to face left", w.bots[0].dir == -1 && hit.moved == true);
        w.advanceBots(2000, 1000);
        check("moves back inward, never out of range", w.bots[0].x == MAP_W - 2);
        w.bots[0].x = 1;
        w.bots[0].dir = -1;
        w.advanceBots(3000, 1000);
        check("hits left wall and clamps at 0", w.bots[0].x == 0);
        check("faces right again", w.bots[0].dir == 1);
    }

    printf("\n=== refillIfSparse ===\n");
    {
        //注意: remainResource 必须和网格一致才算是合法状态,下面都先用 layoutResource 建立。
        World w;
        w.resourceRate = 100;
        w.layoutResource();                                    //全图填满,remain = 4096
        w.refillIfSparse();
        check("above threshold: nothing refilled", w.remainResource == MAP_W * MAP_H);

        World sparse;
        sparse.resourceRate = 0;
        sparse.layoutResource();                               //全空,remain = 0
        sparse.resourceRate = 100;
        sparse.refillIfSparse();
        check("at/below threshold with rate=100: whole map filled", sparse.remainResource == MAP_W * MAP_H);

        World zero;
        zero.resourceRate = 0;
        zero.layoutResource();
        zero.refillIfSparse();
        check("rate=0 scans the map but adds nothing", zero.remainResource == 0);
    }

    printf("\n=== layoutResource density sanity ===\n");
    {
        World w;
        w.resourceRate = 100;
        w.layoutResource();
        check("rate=100 fills every cell", w.remainResource == MAP_W * MAP_H);
        w.resourceRate = 0;
        w.layoutResource();
        check("rate=0 empties every cell", w.remainResource == 0);
        w.resourceRate = 30;
        w.layoutResource();
        double actual = w.remainResource * 100.0 / (MAP_W * MAP_H);
        printf("  INFO  rate=30%% measured %.1f%% (rand, no fixed seed)\n", actual);
        check("rate=30% lands in a sane band", actual > 20.0 && actual < 40.0);
        check("remain equals the cells actually set",
              [&]{ int n = 0; for(int r = 0; r < MAP_H; r++) for(int c = 0; c < MAP_W; c++) if(w.resource[r][c]) n++; return n == w.remainResource; }());
    }

    printf("\nfailed checks: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
