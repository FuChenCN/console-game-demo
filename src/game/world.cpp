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

//从种子格随机长出一条脉。target 是期望格数,四周被别的脉占满时会少一些。
//返回占用的格子下标;调用方嫌太小就负责原样退回。
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
        //候选表用交换删除,顺序无所谓。
        //注意 at 是 frontier 里的位置、idx 是格子的下标,两者量级完全不同,不能共用变量。
        std::uniform_int_distribution<size_t> pick(0, frontier.size() - 1);
        size_t at = pick(rng);
        int idx = frontier[at];
        frontier[at] = frontier.back();
        frontier.pop_back();
        if(cells[idx] != NO_DEPOSIT)      //这个候选可能是早先塞进来的,期间已被占用
            continue;
        occupy(idx % width, idx / width);
    }
    return picked;
}

//一条脉每被挖掉 100#,画面上要从外缘吃掉一格 #。这里先排好"谁先消失":
//每一步都在当前剩余格的外缘里随机挑一个,空洞就从四周往中心收。
//rng 用 seed/id 单独派生,不动地图生成那条序列——同一 seed 的矿脉形状不因这个细节改变。
void assignShrinkRanks(const std::vector<int>& patch, int width, int height,
                       uint32_t seed, uint16_t id,
                       std::vector<uint8_t>& alive, std::vector<uint16_t>& shrinkRank){
    std::minstd_rand rng(seed ^ (static_cast<uint32_t>(id) * 2654435761u));
    for(int idx : patch)
        alive[static_cast<size_t>(idx)] = 1;

    std::vector<int> remaining = patch;
    std::vector<int> boundary;
    boundary.reserve(patch.size());
    for(size_t step = 0; step < patch.size(); step++){
        boundary.clear();
        for(int idx : remaining){
            int x = idx % width;
            int y = idx / width;
            bool onEdge = (x == 0 || !alive[static_cast<size_t>(idx - 1)]) ||
                          (x == width - 1 || !alive[static_cast<size_t>(idx + 1)]) ||
                          (y == 0 || !alive[static_cast<size_t>(idx - width)]) ||
                          (y == height - 1 || !alive[static_cast<size_t>(idx + width)]);
            if(onEdge)
                boundary.push_back(idx);
        }
        //任何非空连通块都有外缘,所以 boundary 不会空。
        std::uniform_int_distribution<size_t> pick(0, boundary.size() - 1);
        int chosen = boundary[pick(rng)];
        shrinkRank[static_cast<size_t>(chosen)] = static_cast<uint16_t>(step + 1);
        alive[static_cast<size_t>(chosen)] = 0;
        for(size_t i = 0; i < remaining.size(); i++){
            if(remaining[i] == chosen){
                remaining[i] = remaining.back();
                remaining.pop_back();
                break;
            }
        }
    }
    for(int idx : patch)
        alive[static_cast<size_t>(idx)] = 0;
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
    //格子里的 id 必须和条目自身对得上;对不上说明列表被动过,直接当不存在。
    return d.id == id ? &d : nullptr;
}

const Deposit* World::findDeposit(int id) const{
    return const_cast<World*>(this)->findDeposit(id);
}

bool World::showsOre(int x, int y) const{
    if(!inBounds(x, y))
        return false;
    size_t idx = cellIndex(x, y, width);
    const Deposit* d = findDeposit(cells[idx]);
    if(d == nullptr || !d->discovered || d->exhausted)
        return false;
    //每格代表 100#:池里还剩几个整百,就还该画出几个 #;名次靠前的从外缘先藏起来。
    int visible = (d->remaining + ORE_PER_CELL - 1) / ORE_PER_CELL;
    if(visible > d->cellCount)
        visible = d->cellCount;
    return static_cast<int>(shrinkRank[idx]) > d->cellCount - visible;
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
    machineCells.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
    shrinkRank.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
    miners.clear();
    deposits.clear();
    minerDeadline = 0;
    regenerate();
}

void World::regenerate(){
    rng.seed(seed);      //从同一条序列头重播:改覆盖率不会顺手把已有矿换个位置
    std::fill(cells.begin(), cells.end(), NO_DEPOSIT);
    std::fill(machineCells.begin(), machineCells.end(), 0);
    std::fill(shrinkRank.begin(), shrinkRank.end(), 0);
    miners.clear();      //矿脉整批换新,旧矿机坐的脉可能已经不在了
    deposits.clear();
    minerDeadline = 0;
    std::vector<uint8_t> aliveCells(cells.size(), 0);   //剥落顺序的临时占位表,整轮生成复用

    const int targetCells = static_cast<int>(static_cast<long long>(width) * height * coverage / 100);
    std::uniform_int_distribution<int> sizeRoll(DEPOSIT_MIN_CELLS, DEPOSIT_MAX_CELLS);
    std::uniform_int_distribution<int> xRoll(0, width - 1);
    std::uniform_int_distribution<int> yRoll(0, height - 1);

    int placed = 0;
    const int attemptLimit = std::max(64, targetCells * 4);   //覆盖率高到长不满时要有出口
    for(int attempt = 0; placed < targetCells && attempt < attemptLimit; attempt++){
        //第一条脉固定从地图中心起长:玩家出生点在中心,否则大地图开局可能走很久碰不到矿。
        bool first = deposits.empty();
        int sx = first ? width / 2 : xRoll(rng);
        int sy = first ? height / 2 : yRoll(rng);
        if(cells[cellIndex(sx, sy, width)] != NO_DEPOSIT)
            continue;

        uint16_t id = static_cast<uint16_t>(deposits.size() + 1);
        std::vector<int> patch = growPatch(sx, sy, sizeRoll(rng), width, height, cells, id, rng);
        if(static_cast<int>(patch.size()) < DEPOSIT_MIN_CELLS){
            for(int idx : patch)                             //长不满最小面积就整条退回
                cells[idx] = NO_DEPOSIT;
            continue;
        }

        Deposit d;
        d.id = id;
        d.cellCount = static_cast<int>(patch.size());
        d.total = d.cellCount * ORE_PER_CELL;
        d.remaining = d.total;
        d.discovered = false;
        d.exhausted = false;
        d.minX = d.maxX = sx;
        d.minY = d.maxY = sy;
        for(int idx : patch){
            int x = idx % width;
            int y = idx / width;
            d.minX = std::min(d.minX, x);
            d.maxX = std::max(d.maxX, x);
            d.minY = std::min(d.minY, y);
            d.maxY = std::max(d.maxY, y);
        }
        //格子数定了才能排剥落顺序:每 100# 从外缘随机吃一格,空洞由外向内收。
        assignShrinkRanks(patch, width, height, seed, id, aliveCells, shrinkRank);
        deposits.push_back(d);
        placed += d.cellCount;

        if(deposits.size() >= 65534)    //格子是 16 位、0 表示无矿,发到这就必须收手
            break;
    }
}

int World::takeFrom(int x, int y, int requested){
    Deposit* d = findDeposit(depositAt(x, y));
    //未探明的脉不能采:玩家看不见它,凭运气挖到等于架空探测机制。
    if(!d || !d->discovered || d->exhausted || requested <= 0)
        return 0;
    int got = std::min(requested, d->remaining);   //余量不足时只给拿得到的部分,不会出现负数
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
                //点亮的是整条脉,不是被碰到的那几格:格子只存归属,一次翻转即见全貌。
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
    //采空即整条脉从网格撤走,不留废矿字符。坐在上面的矿机不需要搬走:
    //它下一次找不到脉,自己就停下来并显示成红色。
    for(size_t i = 0; i < cells.size(); i++)
        if(cells[i] == target.id)
            cells[i] = NO_DEPOSIT;
}

PlaceOutcome World::tryPlaceMiner(int x, int y){
    if(x < 0 || y < 0 || x + MINER_W > width || y + MINER_H > height)
        return {PlaceResult::out_of_bounds, 0};

    //占地六格必须同属一条已探明的脉,而且没被别的矿机占住。
    int vein = depositAt(x, y);
    for(int r = 0; r < MINER_H; r++){
        for(int c = 0; c < MINER_W; c++){
            int here = static_cast<int>(cellIndex(x + c, y + r, width));
            if(cells[here] == NO_DEPOSIT)
                return {PlaceResult::no_vein, 0};
            if(cells[here] != vein)
                return {PlaceResult::not_one_vein, 0};
            if(machineCells[here] != 0)
                return {PlaceResult::blocked, 0};
        }
    }
    Deposit* d = findDeposit(vein);
    if(!d || !d->discovered || d->exhausted)
        return {PlaceResult::undiscovered, 0};

    if(miners.size() >= 65534)
        return {PlaceResult::blocked, 0};        //同样是 16 位 id 的上限

    //拆掉的槽位优先复用,否则追加。id 不能变,所以条目永不删除。
    int slot = -1;
    for(int i = 0; i < static_cast<int>(miners.size()); i++){
        if(miners[i].removed){
            slot = i;
            break;
        }
    }
    Miner m;
    m.x = x;
    m.y = y;
    m.depositId = vein;
    m.buffer = 0;
    m.active = true;
    m.removed = false;
    if(slot >= 0){
        m.id = slot + 1;
        miners[slot] = m;
    }
    else{
        m.id = static_cast<int>(miners.size()) + 1;
        miners.push_back(m);
    }
    for(int r = 0; r < MINER_H; r++)
        for(int c = 0; c < MINER_W; c++)
            machineCells[cellIndex(x + c, y + r, width)] = static_cast<uint16_t>(m.id);

    return {PlaceResult::placed, m.id};
}

Miner* World::minerAt(int x, int y){
    if(!inBounds(x, y))
        return nullptr;
    int id = machineCells[cellIndex(x, y, width)];
    if(id <= 0 || id > static_cast<int>(miners.size()))
        return nullptr;
    Miner& m = miners[id - 1];
    return (!m.removed && m.id == id) ? &m : nullptr;    //同上,序号对不上就当没有
}

const Miner* World::minerAt(int x, int y) const{
    return const_cast<World*>(this)->minerAt(x, y);
}

int World::retrieveAt(int x, int y){
    Miner* m = minerAt(x, y);
    if(!m)
        return 0;
    int got = m->buffer;
    m->buffer = 0;
    //掏空之后占用必然小于上限,按规则恢复工作;脉真没了就继续停着。
    Deposit* d = findDeposit(m->depositId);
    m->active = d != nullptr && !d->exhausted;
    return got;
}

bool World::removeMinerAt(int x, int y){
    Miner* m = minerAt(x, y);
    if(!m)
        return false;
    for(int r = 0; r < MINER_H; r++)
        for(int c = 0; c < MINER_W; c++)
            machineCells[cellIndex(m->x + c, m->y + r, width)] = 0;
    m->removed = true;
    return true;
}

bool World::advanceMiners(unsigned long long now, int stepMs){
    if(now < minerDeadline)
        return false;
    minerDeadline = now + stepMs;
    for(Miner& m : miners){
        if(m.removed)
            continue;
        Deposit* d = findDeposit(m.depositId);
        if(!d || d->exhausted){
            m.active = false;                    //脉采空:红色那档
            continue;
        }
        if(m.buffer >= MINER_BUFFER){
            m.active = false;                    //缓存满了就停,等玩家或以后的无人机来取
            continue;
        }
        //矿机不移动,所以取的是它脚下那一格所属的脉——和玩家手采同一条路径、同一个池子。
        m.buffer += takeFrom(m.x, m.y, MINER_TAKE);
        if(m.buffer >= MINER_BUFFER)
            m.active = false;
    }
    return true;
}
