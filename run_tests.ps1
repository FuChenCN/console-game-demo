# 编译并运行 tests/ 下的全部行为测试。不需要终端,也不用真的开游戏。
# 用法:  pwsh run_tests.ps1     退出码 = 失败的测试文件数(0 = 全过)
#
# 每个测试文件单独链接、单独运行:它们各自带 main(),不能链到一起。
# 链接范围是 src 下除 main.cpp 之外的全部 .cpp——游戏逻辑必须能整体被测试触达,
# 否则会出现"测的是副本、真实代码没验证"的情况。
$ErrorActionPreference = 'Stop'

$root    = $PSScriptRoot
$srcDir  = Join-Path $root 'src'
$logic   = Get-ChildItem -Path $srcDir -Recurse -Filter *.cpp |
           Where-Object { $_.Name -ne 'main.cpp' } | ForEach-Object { $_.FullName }
$tests   = Get-ChildItem -Path (Join-Path $root 'tests') -Filter *_test.cpp

if (-not $logic)  { throw "no logic sources found under $srcDir" }
if (-not $tests)  { throw "no *_test.cpp found under tests" }

$failed = @()
foreach ($test in $tests) {
    $exe = Join-Path $env:TEMP ("pys_" + $test.BaseName + ".exe")
    Write-Host ("--- " + $test.Name + " ---")
    & g++ -std=c++17 -Wall -Wextra -O2 "-I$srcDir" $test.FullName @logic -o $exe
    if ($LASTEXITCODE -ne 0) { throw "test build failed for $($test.Name)" }
    & $exe
    if ($LASTEXITCODE -ne 0) { $failed += $test.Name }
}

if ($failed.Count -eq 0) {
    Write-Host ("all {0} test binaries passed" -f $tests.Count)
} else {
    Write-Host ("FAILED: " + ($failed -join ', '))
}
exit $failed.Count
