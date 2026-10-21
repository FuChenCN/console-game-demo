#ifndef PYS_GAME_TYPES_H
#define PYS_GAME_TYPES_H

//商店商品条目。available=false 的是预留空位:占着列表的一行,但还没实装商品。
struct ShopItem{
    const char* name;
    int price;
    int owned;
    bool available;
    int cap;   //最多能买几份,0 表示不限。机器人用它锁死 32 行,升级用它锁死五档。
};

//一个正在地图上跑动的机器人。dir 是移动方向(+1 向右 / -1 向左),
//justAte 记录上一次迈步有没有吃到资源,决定这一格画 = 还是 < / >。
struct Bot{
    int x;
    int y;
    int dir;
    bool justAte;
};

#endif
