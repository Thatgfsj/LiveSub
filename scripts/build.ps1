# LiveSub 构建脚本（PowerShell 5.1+）
# 用法:
#   .\scripts\build.ps1                 # CPU 源码构建
#   .\scripts\build.ps1 -PrebuiltVulkan # 链接官方预编译 Vulkan DLL（推荐，免装 SDK）
#   .\scripts\build.ps1 -Vulkan         # 源码 Vulkan 构建（需安装 Vulkan SDK）
# 依赖: llvm-mingw（或 MinGW-w64）gcc/clang、CMake >= 3.24、Ninja
param(
    [switch]$PrebuiltVulkan,  # 用 llama.cpp 官方预编译 Vulkan DLL
    [switch]$Vulkan,          # 源码编译 Vulkan（需 Vulkan SDK + glslc）
    [string]$MingwPath = "",  # MinGW 根目录，默认自动探测
    [int]$Jobs = 20
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot

# ---- 探测工具链 ----
if ($MingwPath -eq "") {
    $candidates = @(
        "$env:ProgramFiles\llvm-mingw",
        "C:\llvm-mingw",
        "C:\mingw64"
    )
    foreach ($c in $candidates) {
        if (Test-Path "$c\bin\gcc.exe") { $MingwPath = $c; break }
    }
}
if ($MingwPath -eq "") {
    Write-Host "未找到 MinGW/llvm-mingw 工具链！请安装并指定 -MingwPath" -ForegroundColor Red
    exit 1
}
$env:PATH = "$MingwPath\bin;$env:PATH"
Write-Host "工具链: $MingwPath  gcc $(& gcc --version | Select-Object -First 1)" -ForegroundColor Green

# ---- 预编译 DLL 模式：检查/下载 ----
if ($PrebuiltVulkan) {
    $sdkDir = "$Root\tools\sdk\vulkan"
    if (-not (Test-Path "$sdkDir\llama.dll")) {
        Write-Host "未找到预编译 DLL，下载 llama.cpp 官方 win-vulkan 包..." -ForegroundColor Yellow
        New-Item -ItemType Directory -Force -Path "$Root\tools\sdk" | Out-Null
        $zip = "$Root\tools\sdk\llama-vulkan-bin.zip"
        if (-not (Test-Path $zip)) {
            curl.exe -sL -o $zip "https://github.com/ggml-org/llama.cpp/releases/download/b10217/llama-b10217-bin-win-vulkan-x64.zip"
        }
        Expand-Archive -Path $zip -DestinationPath $sdkDir -Force
    }
    Write-Host "后端: 预编译 Vulkan DLL ($sdkDir)" -ForegroundColor Green
}

# ---- CMake 配置 ----
$buildDir = "$Root\build"
$cmakeArgs = @("-B", $buildDir, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release")
if ($PrebuiltVulkan) {
    $cmakeArgs += "-DLIVESUB_PREBUILT_VULKAN=ON"
} elseif ($Vulkan) {
    if (-not $env:VULKAN_SDK -and -not (Test-Path "C:\VulkanSDK")) {
        Write-Host "Vulkan 源码构建需要 Vulkan SDK（VULKAN_SDK 未设置）" -ForegroundColor Yellow
        Write-Host "提示: 推荐用 -PrebuiltVulkan（免装 SDK）或安装 https://vulkan.lunarg.com/sdk/home#windows"
        exit 1
    }
    $cmakeArgs += "-DGGML_VULKAN=ON"
    Write-Host "后端: Vulkan (源码, GPU)" -ForegroundColor Green
} else {
    Write-Host "后端: CPU" -ForegroundColor Green
}

cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# ---- 构建 ----
cmake --build $buildDir -j $Jobs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ""
Write-Host "构建完成: $buildDir\livesub.exe" -ForegroundColor Green
Write-Host "运行: .\scripts\run.ps1" -ForegroundColor Green
