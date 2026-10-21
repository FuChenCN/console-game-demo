# 编译并运行游戏逻辑的单元测试:不需要终端,也不用真的开游戏。
# 用法:  pwsh run_tests.ps1     退出码即测试结果(0 = 全部通过)
$ErrorActionPreference = 'Stop'

$root    = $PSScriptRoot
$exe     = Join-Path $env:TEMP 'pys_world_test.exe'
$sources = @(
    (Join-Path $root 'tests/world_test.cpp'),
    (Join-Path $root 'src/game/world.cpp')
)

Write-Host ("compiling test: " + (($sources | ForEach-Object { Split-Path $_ -Leaf }) -join ' + '))
& g++ -std=c++17 -Wall -Wextra -O2 "-I$root/src" @sources -o $exe
if ($LASTEXITCODE -ne 0) { throw "test build failed with exit code $LASTEXITCODE" }

& $exe
$code = $LASTEXITCODE
if ($code -eq 0) { Write-Host 'all checks passed' } else { Write-Host "FAILED (exit $code)" }
exit $code
