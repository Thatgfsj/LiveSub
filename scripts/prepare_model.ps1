# 模型准备脚本
# 主路径：已下载官方 GGUF（推荐，与 llama.cpp 官方转换一致）
# 备选路径：把本地 HF safetensors 模型转为 GGUF（需要 Python 3.10+ 与 llama.cpp 转换脚本）
param(
    [switch]$Convert,       # 使用本地转换路径（需已克隆 HF 模型）
    [string]$HfModel = "",  # 本地 HF 模型目录（Convert 模式下必填）
    [string]$LlamaCpp = ""  # llama.cpp 源码目录（Convert 模式下必填）
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$ModelDir = "$Root\models\Qwen3-ASR-1.7B-GGUF"

$MainGGUF = "$ModelDir\Qwen3-ASR-1.7B-Q8_0.gguf"
$Mmproj = "$ModelDir\mmproj-Qwen3-ASR-1.7B-bf16.gguf"

if (-not $Convert) {
    # ---------- 主路径：下载官方 GGUF ----------
    if ((Test-Path $MainGGUF) -and (Test-Path $Mmproj) -and
        ((Get-Item $MainGGUF).Length -gt 1GB) -and ((Get-Item $Mmproj).Length -gt 100MB)) {
        Write-Host "模型已就绪:" -ForegroundColor Green
        Write-Host "  $MainGGUF  ($([math]::Round((Get-Item $MainGGUF).Length/1GB,2)) GB)"
        Write-Host "  $Mmproj  ($([math]::Round((Get-Item $Mmproj).Length/1MB,0)) MB)"
        exit 0
    }
    Write-Host "下载官方 GGUF（llama.cpp 官方组织转换，Q8_0 + BF16 音频编码器）..." -ForegroundColor Yellow
    if (-not (Test-Path $ModelDir)) {
        git clone --depth 1 https://huggingface.co/ggml-org/Qwen3-ASR-1.7B-GGUF $ModelDir
    }
    Push-Location $ModelDir
    git lfs pull --include="Qwen3-ASR-1.7B-Q8_0.gguf,mmproj-Qwen3-ASR-1.7B-bf16.gguf"
    Pop-Location
    Write-Host "完成。若下载失败，可手动下载: https://huggingface.co/ggml-org/Qwen3-ASR-1.7B-GGUF" -ForegroundColor Green
    exit 0
}

# ---------- 备选路径：本地转换 ----------
if ($HfModel -eq "" -or $LlamaCpp -eq "") {
    Write-Host "本地转换需要: -HfModel <HF模型目录> -LlamaCpp <llama.cpp源码目录>" -ForegroundColor Red
    exit 1
}
Write-Host "检查 Python 环境..."
python -c "import transformers, torch; print('transformers', transformers.__version__)" 
if ($LASTEXITCODE -ne 0) {
    Write-Host "需要 Python + transformers + torch（pip install transformers torch）" -ForegroundColor Red
    exit 1
}

$ConvertScript = "$LlamaCpp\convert_hf_to_gguf.py"
if (-not (Test-Path $ConvertScript)) {
    Write-Host "未找到 $ConvertScript，请先克隆 llama.cpp 源码" -ForegroundColor Red
    exit 1
}

New-Item -ItemType Directory -Force -Path $ModelDir | Out-Null

Write-Host "步骤1: 转换主模型 (Q8_0)..." -ForegroundColor Yellow
python $ConvertScript $HfModel --outfile "$ModelDir\Qwen3-ASR-1.7B-Q8_0.gguf" --outtype q8_0
if ($LASTEXITCODE -ne 0) { exit 1 }

Write-Host "步骤2: 转换音频编码器 mmproj (BF16)..." -ForegroundColor Yellow
python $ConvertScript $HfModel --mmproj "$ModelDir\mmproj-Qwen3-ASR-1.7B-bf16.gguf" --outtype bf16
if ($LASTEXITCODE -ne 0) { exit 1 }

Write-Host "转换完成!" -ForegroundColor Green
