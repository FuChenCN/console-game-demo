#include "game/world.h"

//注意: 这里仍使用全局 rand()/srand(),随机源注入是后续步骤的事。

namespace {
//网格是一维的,行优先。(x, y) 到数组下标的换算只允许出现在这一处。
inline size_t cellIndex(int x, int y, int width){
    return static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
}
}

void World::init(int w, int h, int rate){
    width = w;
    height = h;
    resourceRate = rate;
    cells.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
    bots.clear();
    relayout();
}

void World::relayout(){
    remainResource = 0;
    for(int row = 0; row < height; row++){
        for(int column = 0; column < width; column++){
            uint16_t value = (rand() % 100 < resourceRate) ? 1 : 0;
            cells[cellIndex(column, row, width)] = value;
            remainResource += value;
        }
    }
}

bool World::inBounds(int x, int y) const{
    return x >= 0 && x < width && y >= 0 && y < height;
}

bool World::hasResource(int x, int y) const{
    if(!inBounds(x, y))
        return false;
    return cells[cellIndex(x, y, width)] != 0;
}

bool World::collectAt(int x, int y){
    if(!hasResource(x, y))
        return false;
    cells[cellIndex(x, y, width)] = 0;
    remainResource--;
    return true;
}

void World::refillIfSparse(){
    if(remainResource > 50)
        return;
    for(int row = 0; row < height; row++){
        for(int column = 0; column < width; column++){
            size_t idx = cellIndex(column, row, width);
            if(cells[idx] == 0 && rand() % 100 < resourceRate){
                cells[idx] = 1;
                remainResource++;
            }
        }
    }
}

void World::syncBots(int ownedRobots){
    //买一个就多一个。
    while(static_cast<int>(bots.size()) < ownedRobots){
        int index = static_cast<int>(bots.size());
        Bot fresh;
        fresh.x = 1;
        //一只机器人占一行,从 y=0 的第一行开始往下排。"上限=行数"是逐行运行模型的产物,
        //改成放置式矿机之后这条会连同商品表的 cap 一起替换掉。
        fresh.y = index % height;
        fresh.dir = 1;
        fresh.justAte = false;
        bots.push_back(fresh);
    }
}

BotStep World::advanceBots(unsigned long long now, int stepMs){
    BotStep out{false, 0};
    if(now < botDeadline)
        return out;
    //到点才迈步。下一次按当前时间重新排,卡顿掉的份额不会追补成一串脚步。
    botDeadline = now + stepMs;
    out.moved = true;
    for(Bot& bot : bots){
        //先走到下一格,再看这一格有没有资源:吃到才亮出咬合的 =,否则保持张口的 < 或 >。
        bot.x += bot.dir;
        if(bot.x >= width - 1){
            //撞到右侧墙壁,换成朝左的 > 往回走。
            bot.x = width - 1;
            bot.dir = -1;
        }
        else if(bot.x <= 0){
            //左侧墙壁同理,换回 < 向右,如此往复。
            bot.x = 0;
            bot.dir = 1;
        }
        bot.justAte = false;
        //采集走和玩家同一套口径,扣减只有一处实现。
        if(collectAt(bot.x, bot.y)){
            bot.justAte = true;
            out.collected++;
        }
    }
    return out;
}
