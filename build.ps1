# 收集 src 下所有 .cpp 一起编译。加新源文件不用改这个脚本,也不用改 tasks.json。
# 用法:  pwsh build.ps1          产物: .\PYS.exe
$ErrorActionPreference = 'Stop'

$root    = $PSScriptRoot
$srcDir  = Join-Path $root 'src'
$sources = Get-ChildItem -Path $srcDir -Recurse -Filter *.cpp | ForEach-Object { $_.FullName }
$target  = Join-Path $root 'PYS.exe'

if (-not $sources) { throw "no .cpp found under $srcDir" }

Write-Host ("compiling {0} source file(s) -> {1}" -f $sources.Count, $target)
& g++ -std=c++17 -Wall -Wextra -O2 "-I$srcDir" @sources -o $target

if ($LASTEXITCODE -ne 0) { throw "g++ failed with exit code $LASTEXITCODE" }
Write-Host "ok: $((Get-Item $target).Length) bytes"
