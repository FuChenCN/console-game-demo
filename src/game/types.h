#ifndef PYS_GAME_TYPES_H
#define PYS_GAME_TYPES_H

//商店商品条目。available=false 的是预留空位:占着列表的一行,但还没实装商品。
struct ShopItem{
    const char* name;
    int price;
    int owned;
    bool available;
    int cap;   //最多能买几份,0 表示不限。矿机不限量,升级锁死五档。
};

#endif
