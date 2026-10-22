#include "platform/launch_args.h"

#include <exception>    //catch 的是 std::exception 基类,显式包含,不指望 stdexcept 顺带提供
#include <stdexcept>
#include <string>       //本文件直接用 std::string / std::stoul

namespace {

//整段都必须是数字才算有效:used == text.size() 这条能挡住 "12abc" 被静默当成 12,
//超范围的溢出由 out_of_range 落到 catch 里。
bool parseNumber(const std::string& text, unsigned long& out){
    if(text.empty())
        return false;
    try{
        size_t used = 0;
        out = std::stoul(text, &used);
        return used == text.size();
    }
    catch(const std::exception&){
        return false;
    }
}

}

LaunchOptions parseLaunchArgs(int argc, const char* const* argv,
                               const std::string& envSeed,
                               uint32_t clockSeed){
    LaunchOptions opt;
    opt.seed = clockSeed;          //默认用调用方给的时钟值
    bool seedFromArg = false;

    for(int index = 1; index < argc; index++){
        const std::string key(argv[index]);
        if(key == "--help" || key == "-h"){
            opt.showHelp = true;
            continue;
        }
        if(index + 1 >= argc){     //选项在末尾、后面没有值
            opt.notice = "missing value after " + key;
            continue;
        }
        const std::string value(argv[index + 1]);
        if(key == "--seed"){
            unsigned long parsed = 0;
            if(parseNumber(value, parsed)){
                opt.seed = static_cast<uint32_t>(parsed);
                seedFromArg = true;
            }
            else
                opt.notice = "bad --seed value, using the clock";
        }
        else if(key == "--size"){
            const size_t split = value.find('x');
            unsigned long w = 0;
            unsigned long h = 0;
            if(split != std::string::npos &&
               parseNumber(value.substr(0, split), w) &&
               parseNumber(value.substr(split + 1), h)){
                opt.mapW = static_cast<int>(w);
                opt.mapH = static_cast<int>(h);
                opt.sizeGiven = true;
            }
            else
                opt.notice = "bad --size value, expected WIDTHxHEIGHT";
        }
        else{
            //未知选项不消费后面的 token:它到底带不带值无从判断,吞一口会把紧跟其后的合法参数一起吃掉。
            opt.notice = "unknown option " + key;
            continue;
        }
        index++;                   //已识别选项的值在这里消费掉
    }

    //环境变量是第二优先级:命令行没给种子、且命令行那边没报错时才用。
    //命令行显式写错时不悄悄改用环境变量,免得用户以为用的是自己敲的那个数。
    if(!seedFromArg && opt.notice.empty() && !envSeed.empty()){
        unsigned long parsed = 0;
        if(parseNumber(envSeed, parsed))
            opt.seed = static_cast<uint32_t>(parsed);
        else
            opt.notice = "bad PYS_SEED value, using the clock";
    }
    return opt;
}
