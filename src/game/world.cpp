#include "game/world.h"

//注意: 这里还用全局 rand()/srand(),随机源注入是路线第 3 步的事。

void World::layoutResource(){
    remainResource = 0;
    for(int row = 0; row < MAP_H; row++){
        for(int column = 0; column < MAP_W; column++){
            resource[row][column] = (rand() % 100 < resourceRate);
            if(resource[row][column])
                remainResource++;
        }
    }
}

bool World::collectAt(int x, int y){
    if(!resource[y][x])
        return false;
    resource[y][x] = false;
    remainResource--;
    return true;
}

void World::refillIfSparse(){
    if(remainResource > 50)
        return;
    for(int row = 0; row < MAP_H; row++){
        for(int column = 0; column < MAP_W; column++){
            if(!resource[row][column] && rand() % 100 < resourceRate){
                resource[row][column] = true;
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
        //一行只站一个,从 y=0 的第一行开始依次往下排;排满 MAP_H 行后 cap 就不让再买了。
        fresh.y = index % MAP_H;
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
        if(bot.x >= MAP_W - 1){
            //撞到右侧墙壁,换成朝左的 > 往回走。
            bot.x = MAP_W - 1;
            bot.dir = -1;
        }
        else if(bot.x <= 0){
            //左侧墙壁同理,换回 < 向右,如此往复。
            bot.x = 0;
            bot.dir = 1;
        }
        bot.justAte = false;
        if(resource[bot.y][bot.x]){
            //机器人采到的资源走和玩家同一笔账,但背包计数交给调用方加,World 不碰。
            resource[bot.y][bot.x] = false;
            remainResource--;
            bot.justAte = true;
            out.collected++;
        }
    }
    return out;
}
