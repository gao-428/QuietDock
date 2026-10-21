param([string]$Compiler = 'C:\msys64\ucrt64\bin\g++.exe')
$ErrorActionPreference = 'Stop'
$project = $PSScriptRoot
if (-not (Test-Path -LiteralPath $Compiler)) { throw "找不到编译器：$Compiler，请传入 -Compiler <g++.exe>。" }
New-Item -ItemType Directory -Path (Join-Path $project 'release') -Force | Out-Null
& $Compiler (Join-Path $project 'QuietDock.cpp') -o (Join-Path $project 'release\QuietDock.exe') -std=c++17 -O2 -s -municode -mwindows -static -static-libgcc -static-libstdc++ '-Wl,--dynamicbase,--nxcompat' -lcomctl32 -lcomdlg32 -lshell32 -lshlwapi -lole32 -loleaut32 -luuid -lgdi32 -lgdiplus -ldwmapi -ladvapi32
if ($LASTEXITCODE -ne 0) { throw "编译失败：$LASTEXITCODE" }
Write-Output "已生成 $project\release\QuietDock.exe"
