//针对真实 parseLaunchArgs 的行为测试:链接 src/platform/launch_args.cpp。
#include "platform/launch_args.h"

#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;
static void check(const char* what, bool ok){
    printf("  %s  %s\n", ok ? "PASS" : "FAIL", what);
    if(!ok) failures++;
}

//把命令行拼成 argv。写成这样是为了让用例读起来像真实命令。
static LaunchOptions run(std::vector<std::string> words, const std::string& envSeed, uint32_t clockSeed){
    std::vector<const char*> argv;
    argv.push_back("PYS.exe");
    for(const auto& w : words)
        argv.push_back(w.c_str());
    return parseLaunchArgs(static_cast<int>(argv.size()), argv.data(), envSeed, clockSeed);
}

int main(){
    printf("=== defaults ===\n");
    {
        LaunchOptions o = run({}, "", 4242u);
        check("no args uses the clock seed", o.seed == 4242u);
        check("no args means size not given", o.sizeGiven == false);
        check("no help", o.showHelp == false);
        check("nothing to report", o.notice.empty());
    }

    printf("\n=== --seed ===\n");
    {
        check("valid seed wins over clock", run({"--seed", "123"}, "", 4242u).seed == 123u);
        LaunchOptions bad = run({"--seed", "abc"}, "", 4242u);
        check("garbage seed falls back to clock", bad.seed == 4242u);
        check("garbage seed is reported", !bad.notice.empty());
        LaunchOptions half = run({"--seed", "12abc"}, "", 4242u);
        check("trailing junk is not silently read as 12", half.seed == 4242u);
        LaunchOptions huge = run({"--seed", "99999999999999999999"}, "", 4242u);
        check("out of range falls back", huge.seed == 4242u);
        LaunchOptions tail = run({"--seed"}, "", 4242u);
        check("option with no value is reported", !tail.notice.empty());
        check("option with no value keeps the clock seed", tail.seed == 4242u);
    }

    printf("\n=== PYS_SEED ===\n");
    {
        check("env seed applies when no --seed", run({}, "77", 4242u).seed == 77u);
        check("command line beats env", run({"--seed", "5"}, "77", 4242u).seed == 5u);
        LaunchOptions badEnv = run({}, "oops", 4242u);
        check("bad env is reported", !badEnv.notice.empty());
        check("bad env falls back to clock", badEnv.seed == 4242u);
    }

    printf("\n=== --size ===\n");
    {
        LaunchOptions ok = run({"--size", "256x128"}, "", 1u);
        check("valid size parsed", ok.sizeGiven && ok.mapW == 256 && ok.mapH == 128);
        check("valid size has no complaint", ok.notice.empty());

        //解析层只管格式:0 是"格式对、语义非法",要原样传下去,由 World::init 按契约拒绝。
        LaunchOptions zero = run({"--size", "0x8"}, "", 1u);
        check("zero size is passed through, not swallowed here",
              zero.sizeGiven && zero.mapW == 0 && zero.mapH == 8);
        check("zero size raises no parse notice", zero.notice.empty());

        LaunchOptions junk = run({"--size", "12abc"}, "", 1u);
        check("malformed size is reported", !junk.notice.empty());
        check("malformed size falls back to default (not given)", junk.sizeGiven == false);
        LaunchOptions noX = run({"--size", "512"}, "", 1u);
        check("missing separator is reported", !noX.notice.empty());
    }

    printf("\n=== --help and unknown ===\n");
    {
        check("--help sets flag", run({"--help"}, "", 1u).showHelp == true);
        check("-h sets flag", run({"-h"}, "", 1u).showHelp == true);
        LaunchOptions unknown = run({"--nope"}, "", 1u);
        check("unknown option is reported", !unknown.notice.empty());
        check("unknown option does not stop the rest",
              run({"--nope", "--seed", "9"}, "", 1u).seed == 9u);
    }

    printf("\nfailed checks: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
