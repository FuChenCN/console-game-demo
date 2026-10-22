#ifndef PYS_PLATFORM_LAUNCH_ARGS_H
#define PYS_PLATFORM_LAUNCH_ARGS_H

#include <cstdint>
#include <string>

//启动时可以指定的地图参数。在"世界规模选择"界面做出来之前,这是复现地图和试大图形的唯一入口;
//那之后把这里的默认值换成界面选择的结果即可,解析逻辑不必重写。
struct LaunchOptions{
    uint32_t seed = 0;
    int mapW = 0;
    int mapH = 0;
    bool sizeGiven = false;    //分清"没写 --size"和"写了但是非法值":后者要交给 World 拒绝
    bool showHelp = false;
    std::string notice;        //坏输入的说明,启动阶段写 stderr
};

//解析命令行。clockSeed 由调用方给(通常是当前时间),envSeed 是 PYS_SEED 的内容(没有就传空串)。
//两者都做成参数而不是函数内部去读时钟和环境变量,是为了让这个解析能被测试覆盖。
//规则:格式错的输入回落默认并在 notice 里说明;格式对但语义非法的值(比如尺寸 0)照实传下去,
//由 World::init 按它的契约拒绝。
LaunchOptions parseLaunchArgs(int argc, const char* const* argv,
                              const std::string& envSeed,
                              uint32_t clockSeed);

#endif
