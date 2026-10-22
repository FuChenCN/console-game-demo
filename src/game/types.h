#ifndef PYS_GAME_TYPES_H
#define PYS_GAME_TYPES_H

//商店商品条目。available=false 的是预留空位:占着列表的一行,但还没实装商品。
struct ShopItem{
    const char* name;
    int price;
    int owned;
    bool available;
    int cap;   //最多能买几份,0 表示不限。升级用它锁死五档;机器人那份上限来自地图行数。
};

//一个正在地图上跑动的机器人。dir 是移动方向(+1 向右 / -1 向左),
//justAte 记录上一次迈步有没有吃到资源,决定这一格画 = 还是 < / >。
struct Bot{
    int x;
    int y;
    int dir;
    bool justAte;
};

//一条矿脉。矿不是逐格存放的:整条脉共用一个储量池。
//生成时按 面积 x 每格储量 算出总量,此后所有采集(玩家手采、机器人、以后的矿机与无人机)
//都从 remaining 里扣。格子只记录"我属于哪条脉",所以采空是整条脉一起消失,
//而不是一格一格被挖空——这也是枯竭不会产生字符堆砌的原因。
struct Deposit{
    int id;             //稳定标识:格子里存的就是它。矿脉只标记枯竭、不删除,否则 id 会整体错位。
    int remaining;      //还剩多少 #
    int total;          //生成时的总量,用于显示"采掉了多少"
    bool discovered;    //未被探测到的矿脉完全不显示,和空地无异
    bool exhausted;     //采空:网格同时清掉,后续不再能采
    int cellCount;      //占了多少格
    int minX, minY, maxX, maxY;   //包围盒,视口裁剪和范围查询用
};

#endif
