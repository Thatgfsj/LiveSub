# LiveSub 运行脚本
# 用法:
#   .\scripts\run.ps1                     # 正常启动（麦克风）
#   .\scripts\run.ps1 -Settings           # 启动并打开设置窗
#   .\scripts\run.ps1 -Wav <file.wav>     # 用 wav 模拟麦克风（测试）
param(
    [switch]$Settings,
    [string]$Wav = "",
    [string]$Config = "config.ini"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot

# 运行时 DLL：build 目录（预编译模式已复制 DLL）+ MinGW 运行时
foreach ($c in @("$Root\build", "$Root\tools\sdk\vulkan",
                 "$env:ProgramFiles\llvm-mingw\bin", "C:\llvm-mingw\bin",
                 "O:\llvm-mingw\mingw64\bin", "C:\mingw64\bin")) {
    if (Test-Path $c) { $env:PATH = "$c;$env:PATH" }
}

# GPU 版优先（build-vk = 预编译 Vulkan DLL 构建），没有则用 CPU 版（build）
$exe = "$Root\build-vk\livesub.exe"
if (-not (Test-Path $exe)) { $exe = "$Root\build\livesub.exe" }
if (-not (Test-Path $exe)) {
    Write-Host "未找到 $exe，请先运行 .\scripts\build.ps1" -ForegroundColor Red
    exit 1
}

$args = @("--config", "$Root\$Config")
if ($Settings) { $args += "--settings" }
if ($Wav -ne "") { $args += @("--wav", $Wav) }

Write-Host "启动 LiveSub ..." -ForegroundColor Green
& $exe @args
