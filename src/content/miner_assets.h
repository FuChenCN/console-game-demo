#ifndef PYS_CONTENT_MINER_ASSETS_H
#define PYS_CONTENT_MINER_ASSETS_H

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "game/types.h"   //MINER_W / MINER_H:几何属于 game 层,这里只管画成什么

//矿机在地图上的样子。标签是 "miner" 五个字母,塞进 3x2 的占地就是 min / er 两行。
//只用 ASCII:和标题页同一个理由,宿主终端常是 GBK/936,扩展字符会被当成双字节前导码。
inline constexpr char MINER_LABEL[MINER_H][MINER_W] = {
    {'m', 'i', 'n'},
    {'e', 'r', ' '},
};

//三种状态的颜色。刻意不是一路亮度,而且有优先级:
//  缓存满 亮黄   —— 优先于"枯竭":哪怕脉已挖空,那 30# 还等人来取,红色会把它藏起来
//  矿脉枯 暗红   —— 没亮度,看着就是死物;"枯竭变红"要的是这个感觉
//  工作中 亮绿   —— 正常在跑
inline constexpr BYTE MINER_COLOR_WORKING = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
inline constexpr BYTE MINER_COLOR_FULL = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
inline constexpr BYTE MINER_COLOR_DRAINED = FOREGROUND_RED;

//画哪一档。缓存满排第一: "该来取货"比"这条脉没了"更值得让人看见。
//参数都是纯状态,是为了让颜色规则能脱离终端单独测。
inline BYTE minerColor(bool cacheFull, bool veinExhausted, bool active){
    if(cacheFull)
        return MINER_COLOR_FULL;
    if(veinExhausted)
        return MINER_COLOR_DRAINED;
    if(!active)
        return MINER_COLOR_FULL;
    return MINER_COLOR_WORKING;
}

#endif
