#include "game/world.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <random>
#include <stdexcept>
#include <vector>

namespace {
//网格是一维的、行优先。(x, y) 到下标的换算只允许出现在这一处。
inline size_t cellIndex(int x, int y, int width){
    return static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
}

//从种子格随机向外长出一条脉。target 是期望格数,四周被别的脉占满时会少一些。
//返回占用的格子下标;调用方若嫌太小,负责把它们原样退回。
std::vector<int> growPatch(int seedX, int seedY, int target, int width, int height,
                           std::vector<uint16_t>& cells, uint16_t id, std::minstd_rand& rng){
    const int dx[4] = {1, -1, 0, 0};
    const int dy[4] = {0, 0, 1, -1};
    std::vector<int> picked;
    std::vector<int> frontier;

    auto occupy = [&](int x, int y){
        int here = static_cast<int>(cellIndex(x, y, width));
        cells[here] = id;
        picked.push_back(here);
        for(int k = 0; k < 4; k++){
            int nx = x + dx[k];
            int ny = y + dy[k];
            if(nx >= 0 && nx < width && ny >= 0 && ny < height &&
               cells[cellIndex(nx, ny, width)] == NO_DEPOSIT)
                frontier.push_back(static_cast<int>(cellIndex(nx, ny, width)));
        }
    };

    occupy(seedX, seedY);
    while(static_cast<int>(picked.size()) < target && !frontier.empty()){
        //候选表用交换删除:顺序无所谓,省掉一次 memmove。
        //注意 at 是 frontier 里的位置、idx 是格子的下标,两者量级完全不同,不能共用一个变量。
        std::uniform_int_distribution<size_t> pick(0, frontier.size() - 1);
        size_t at = pick(rng);
        int idx = frontier[at];
        frontier[at] = frontier.back();
        frontier.pop_back();
        //这个候选可能是早先塞进来的,期间已被别的脉占用。
        if(cells[idx] != NO_DEPOSIT)
            continue;
        occupy(idx % width, idx / width);
    }
    return picked;
}
}

bool World::inBounds(int x, int y) const{
    return x >= 0 && x < width && y >= 0 && y < height;
}

int World::depositAt(int x, int y) const{
    if(!inBounds(x, y))
        return NO_DEPOSIT;
    return cells[cellIndex(x, y, width)];
}

Deposit* World::findDeposit(int id){
    if(id <= NO_DEPOSIT || id > static_cast<int>(deposits.size()))
        return nullptr;
    Deposit& d = deposits[id - 1];
    //格子里的 id 必须和条目自身对上:对不上说明列表被动过,直接当不存在处理。
    return d.id == id ? &d : nullptr;
}

const Deposit* World::findDeposit(int id) const{
    return const_cast<World*>(this)->findDeposit(id);
}

bool World::showsOre(int x, int y) const{
    const Deposit* d = findDeposit(depositAt(x, y));
    return d != nullptr && d->discovered && !d->exhausted;
}

int World::remainingUnits() const{
    int sum = 0;
    for(const Deposit& d : deposits)
        if(!d.exhausted)
            sum += d.remaining;
    return sum;
}

void World::init(int w, int h, int coveragePercent, uint32_t s){
    if(w <= 0 || h <= 0)
        throw std::invalid_argument("world dimensions must be positive");
    width = w;
    height = h;
    coverage = coveragePercent;
    seed = s;
    rng.seed(s);                             //同一 seed 必得同一张图
    cells.assign(static_cast<size_t>(width) * static_cast<size_t>(height), NO_DEPOSIT);
    bots.clear();                            //新世界不带着上一张图的机器人
    deposits.clear();
    botDeadline = 0;
    regenerate();
}

void World::regenerate(){
    rng.seed(seed);      //从同一条序列头重播:改覆盖率不会顺手把已有矿格换个位置
    std::fill(cells.begin(), cells.end(), NO_DEPOSIT);
    deposits.clear();
    botDeadline = 0;

    const int targetCells = static_cast<int>(static_cast<long long>(width) * height * coverage / 100);
    std::uniform_int_distribution<int> sizeRoll(DEPOSIT_MIN_CELLS, DEPOSIT_MAX_CELLS);
    std::uniform_int_distribution<int> xRoll(0, width - 1);
    std::uniform_int_distribution<int> yRoll(0, height - 1);

    int placed = 0;
    //覆盖率再高也长不满一张图的场合要有出口:按目标格数放宽尝试次数,再不成就是少铺。
    const int attemptLimit = std::max(64, targetCells * 4);
    for(int attempt = 0; placed < targetCells && attempt < attemptLimit; attempt++){
        //第一条脉固定从地图中心起长。玩家出生点就在中心,不这么保证的话,
        //512x512 上开局可能要走很久才碰到第一块矿。
        bool first = deposits.empty();
        int sx = first ? width / 2 : xRoll(rng);
        int sy = first ? height / 2 : yRoll(rng);
        if(cells[cellIndex(sx, sy, width)] != NO_DEPOSIT)
            continue;

        uint16_t id = static_cast<uint16_t>(deposits.size() + 1);
        std::vector<int> picked = growPatch(sx, sy, sizeRoll(rng), width, height, cells, id, rng);
        if(static_cast<int>(picked.size()) < DEPOSIT_MIN_CELLS){
            //长不满最小面积就整条退回,不留"半个矿脉"这种奇怪东西。
            for(int idx : picked)
                cells[idx] = NO_DEPOSIT;
            continue;
        }

        Deposit d;
        d.id = id;
        d.cellCount = static_cast<int>(picked.size());
        d.total = d.cellCount * ORE_PER_CELL;
        d.remaining = d.total;
        d.discovered = false;
        d.exhausted = false;
        d.minX = d.maxX = sx;
        d.minY = d.maxY = sy;
        for(int idx : picked){
            int x = idx % width;
            int y = idx / width;
            d.minX = std::min(d.minX, x);
            d.maxX = std::max(d.maxX, x);
            d.minY = std::min(d.minY, y);
            d.maxY = std::max(d.maxY, y);
        }
        deposits.push_back(d);
        placed += d.cellCount;

        //id 是 16 位、且 0 表示"无矿",用到 65534 就必须收手:再发号会把矿脉标到空气上。
        if(deposits.size() >= 65534)
            break;
    }
}

int World::takeFrom(int x, int y, int requested){
    Deposit* d = findDeposit(depositAt(x, y));
    //未探明的脉不能采:玩家看不见它,凭运气挖到等于架空探测机制。
    if(!d || !d->discovered || d->exhausted || requested <= 0)
        return 0;
    int got = std::min(requested, d->remaining);   //池余量不足时只给拿得到的部分,不会出现负数
    if(got <= 0)
        return 0;
    d->remaining -= got;
    if(d->remaining == 0)
        exhaust(*d);
    return got;
}

int World::playerTake(int x, int y){
    std::uniform_int_distribution<int> roll(PLAYER_TAKE_MIN, PLAYER_TAKE_MAX);
    return takeFrom(x, y, roll(rng));
}

int World::probeAt(int cx, int cy){
    int found = 0;
    for(int y = cy - PROBE_HALF; y <= cy + PROBE_HALF; y++){
        for(int x = cx - PROBE_HALF; x <= cx + PROBE_HALF; x++){
            Deposit* d = findDeposit(depositAt(x, y));
            if(d && !d->discovered){
                //揭示的是整条脉,不是被探到的那几格:格子只存归属 id,
                //所以点亮一次就看见全貌,不需要 flood fill。
                d->discovered = true;
                found++;
            }
        }
    }
    return found;
}

void World::exhaust(Deposit& target){
    target.exhausted = true;
    target.remaining = 0;
    //采空即整条脉从网格上撤走,不留"废矿"字符:枯竭的表现是矿机标签变红,不是地面铺一层痕迹。
    for(size_t i = 0; i < cells.size(); i++)
        if(cells[i] == target.id)
            cells[i] = NO_DEPOSIT;
}

void World::syncBots(int ownedRobots){
    while(static_cast<int>(bots.size()) < ownedRobots){
        int index = static_cast<int>(bots.size());
        Bot fresh;
        fresh.x = 1;
        //一只机器人占一行,从 y=0 开始往下排。
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
    botDeadline = now + stepMs;
    out.moved = true;
    for(Bot& bot : bots){
        //先走到下一格,再看这一格所在脉还有没有矿:吃到才亮出咬合的 =。
        bot.x += bot.dir;
        if(bot.x >= width - 1){
            bot.x = width - 1;
            bot.dir = -1;
        }
        else if(bot.x <= 0){
            bot.x = 0;
            bot.dir = 1;
        }
        bot.justAte = false;
        //机器人和玩家从同一个池取水,不各存一份账。
        if(takeFrom(bot.x, bot.y, BOT_TAKE) > 0){
            bot.justAte = true;
            out.collected++;
        }
    }
    return out;
}
