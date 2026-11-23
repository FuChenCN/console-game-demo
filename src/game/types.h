#ifndef PYS_GAME_TYPES_H
#define PYS_GAME_TYPES_H

//商店商品条目。available=false 的是预留空位:占着列表的一行,但还没实装商品。
struct ShopItem{
    const char* name;
    int price;
    int owned;
    bool available;
    int cap;   //最多能买几份,0 表示不限。矿机不限,升级用它锁死五档。
};

//矿机占地。放在 game 层,因为 World 的放置规则要拿它判 3x2 边界;
//字模与状态色属于表现,留在 content/miner_assets.h。
inline constexpr int MINER_W = 3;
inline constexpr int MINER_H = 2;

//一条矿脉。矿不是逐格存放的:整条脉共用一个储量池。
//生成时按 面积 x 每格储量 算出总量,此后所有采集(玩家手采、矿机、以后的无人机)
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

//一台放在地图上的矿机。它不移动:只负责坐在脉上按节拍从池里取矿,
//玩家得自己走过去把缓存取走。
//
//和矿脉同样的理由:矿机列表只标记移除、不删除条目,占位格子里存的是它的序号,
//删除会让序号错位。拆除后的槽位会被新矿机复用。
struct Miner{
    int id;             //1 起,0 保留给"此格没有矿机"
    int x, y;           //左上角。占地 MINER_W x MINER_H
    int depositId;      //它坐在哪条脉上;脉枯竭后这台机器就没料可采了
    int buffer;         //缓存里已存多少 #
    bool active;        //缓存装满后置 false,取货后恢复
    bool removed;       //已拆除,槽位等待复用
};

#endif
