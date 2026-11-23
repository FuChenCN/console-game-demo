//矿机标签颜色优先级的纯规则测试:只链接头文件里的 minerColor,不开终端。
//锁住一条产品规则——缓存满必须比"脉枯"优先:一台装满 30# 却停在枯脉上的矿机
//如果画成红色,玩家会以为没货可取,把缓存漏在那儿。
#include "content/miner_assets.h"

#include <cstdio>

static int failures = 0;
static void check(const char* what, bool ok){
    printf("  %s  %s\n", ok ? "PASS" : "FAIL", what);
    if(!ok) failures++;
}

int main(){
    printf("=== miner label colour priority ===\n");
    check("working machine is green", minerColor(false, false, true) == MINER_COLOR_WORKING);
    check("full cache on a live vein is yellow", minerColor(true, false, false) == MINER_COLOR_FULL);
    check("full cache beats an exhausted vein", minerColor(true, true, false) == MINER_COLOR_FULL);
    check("partial cache on an exhausted vein is red", minerColor(false, true, false) == MINER_COLOR_DRAINED);
    check("idle without a full cache stays yellow", minerColor(false, false, false) == MINER_COLOR_FULL);

    printf("\nfailed checks: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
