# LiveSub 发布打包脚本
# 生成独立软件目录 dist\LiveSub\（可整个复制/压缩分发，双击 livesub.exe 运行）:
#   dist\LiveSub\
#   ├─ livesub.exe                主程序（GUI，双击运行）
#   ├─ *.dll                      运行时库（llama/ggml/vulkan + MinGW 运行时）
#   ├─ model\                     ★ 模型文件夹
#   │  ├─ Qwen3-ASR-1.7B-Q8_0.gguf
#   │  └─ mmproj-Qwen3-ASR-1.7B-bf16.gguf
#   ├─ config.ini                 配置（自动生成，model 指向 model\）
#   └─ 使用说明.txt
param(
    [switch]$UseCpu,          # 打包 CPU 版（默认打包 GPU 预编译版）
    [string]$MingwPath = ""   # MinGW 根目录（复制运行时 DLL 用），默认自动探测
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Dist = "$Root\dist\LiveSub"

if ($UseCpu) {
    $BuildDir = "$Root\build"
} else {
    $BuildDir = "$Root\build-vk"
}
$Exe = "$BuildDir\livesub.exe"
if (-not (Test-Path $Exe)) {
    Write-Host "未找到 $Exe，请先构建（build.ps1 [-PrebuiltVulkan]）" -ForegroundColor Red
    exit 1
}

# ---- 1. 清空并创建软件目录 ----
if (Test-Path $Dist) { Remove-Item $Dist -Recurse -Force }
New-Item -ItemType Directory -Force -Path $Dist | Out-Null
New-Item -ItemType Directory -Force -Path "$Dist\model" | Out-Null

# ---- 2. 复制主程序 ----
Copy-Item $Exe $Dist
Write-Host "主程序: livesub.exe" -ForegroundColor Green

# ---- 3. 复制运行时 DLL ----
# MinGW 运行时（libstdc++ 等，任何模式都需要）
if ($MingwPath -eq "") {
    foreach ($c in @("$env:ProgramFiles\llvm-mingw", "C:\llvm-mingw", "C:\mingw64")) {
        if (Test-Path "$c\bin\gcc.exe") { $MingwPath = $c; break }
    }
}
if ($MingwPath -ne "") {
    foreach ($dll in @("libstdc++-6.dll", "libgcc_s_seh-1.dll", "libwinpthread-1.dll")) {
        if (Test-Path "$MingwPath\bin\$dll") { Copy-Item "$MingwPath\bin\$dll" $Dist }
    }
    Write-Host "MinGW 运行时: 已复制" -ForegroundColor Green
} else {
    Write-Host "警告: 未找到 MinGW 运行时（-MingwPath），打包可能无法运行" -ForegroundColor Yellow
}

if (-not $UseCpu) {
    # GPU 版：预编译 DLL（build 目录已有构建时复制的副本）
    Get-ChildItem "$BuildDir\*.dll" | Copy-Item -Destination $Dist
    Write-Host "GPU DLL: $((Get-ChildItem $Dist\*.dll).Count) 个" -ForegroundColor Green
}

# ---- 4. 复制模型到 model\ ----
$modelFiles = @(
    "$Root\models\Qwen3-ASR-1.7B-GGUF\Qwen3-ASR-1.7B-Q8_0.gguf",
    "$Root\models\Qwen3-ASR-1.7B-GGUF\mmproj-Qwen3-ASR-1.7B-bf16.gguf"
)
foreach ($m in $modelFiles) {
    if (Test-Path $m) {
        Copy-Item $m "$Dist\model\"
        Write-Host "模型: $(Split-Path $m -Leaf) ($([math]::Round((Get-Item $m).Length/1GB,2)) GB)" -ForegroundColor Green
    } else {
        Write-Host "警告: 缺少 $m（模型未下载，请先运行 prepare_model.ps1）" -ForegroundColor Yellow
    }
}

# ---- 5. 生成 config.ini（model 路径指向 model\） ----
$config = @"
; LiveSub 配置文件
[audio]
sample_rate = 48000
device_id =
input_boost_db = 0.000000

[vad]
threshold_db = -52.000000
margin_db = 12.000000
min_speech_ms = 250
silence_ms = 800

[asr]
model_path = model\Qwen3-ASR-1.7B-Q8_0.gguf
mmproj_path = model\mmproj-Qwen3-ASR-1.7B-bf16.gguf
n_threads = 18
gpu_layers = 999
n_batch = 256
chunk_ms = 6000
hop_ms = 800
max_new_tokens = 128
prompt = Transcribe the audio.

[ui]
font_family = Microsoft YaHei UI
font_size = 42.000000
font_color = #FFFFFF
bg_color = #33000000
window_w = 1280
window_h = 260
center_x = 0
center_y = 300
max_lines = 2
always_on_top = true
click_through = false
show_status = true
fade_in_ms = 300
fade_out_ms = 500
fps = 30

[output]
write_srt = false
srt_path = subtitles.srt
write_text = true
text_path = subtitles.txt

[log]
log_level = 1
"@
[System.IO.File]::WriteAllText("$Dist\config.ini", $config, (New-Object System.Text.UTF8Encoding $false))

# ---- 6. 使用说明 ----
$readme = @"
LiveSub 直播实时字幕（本地 Qwen3-ASR）

【使用】
1. 双击 livesub.exe 启动（无窗口，右下角托盘出现图标）
   - 蓝 = 就绪   绿 = 识别中   红 = 出错
2. 对麦克风说话，屏幕底部出现实时字幕
3. 右键托盘图标 → 设置… 可调整麦克风/字幕样式
   双击托盘图标同样打开设置
4. 关闭：右键托盘 → 退出

【记录讲话稿】
1. 右键托盘图标 → "开始记录讲话稿"
2. 此后每句定稿的语音会实时写入桌面文件：
   讲话记录_年月日_时分秒.txt（如 讲话记录_20260801_171405.txt）
3. 结束：右键托盘 → "结束记录讲话稿"（文件自动收尾）

【OBS 集成】
OBS → 来源 → 窗口捕获 → 选择 "LiveSub 字幕"
（OBS 31+ 用 WGC 捕获可勾选"允许透明度"）

【排错】
- 托盘图标变红：右键看提示，或查看 livesub.log
- 字幕窗口显示"就绪 xx dB（阈值 xx dB）"：说话时电平应明显升高；
  若始终很低，检查 Windows 麦克风权限/音量
- 无法识别：设置中调低 VAD 阈值（如 -42）

【目录说明】
- model\    模型文件（Qwen3-ASR-1.7B，勿删除）
- config.ini 配置文件（可备份）
- livesub.log 运行日志
- subtitles.txt 当前字幕（OBS 文本源备选）
"@
[System.IO.File]::WriteAllText("$Dist\使用说明.txt", $readme, (New-Object System.Text.UTF8Encoding $false))

Write-Host ""
Write-Host "打包完成: $Dist" -ForegroundColor Green
Write-Host "整个目录可复制/压缩到任意 Windows 11 电脑直接运行（无需安装任何环境）。" -ForegroundColor Green
