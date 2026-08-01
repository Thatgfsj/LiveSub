# 构建指南（Windows 11）

## 环境要求

| 组件 | 要求 | 说明 |
|---|---|---|
| 工具链 | llvm-mingw 或 MinGW-w64（gcc ≥ 12） | 本机已装：`O:\llvm-mingw\mingw64`（GCC 14.2） |
| CMake | ≥ 3.24 | 本机已装 3.30.4 |
| Ninja | 推荐 | 本机已装 |
| Git | 任意 | 本机已装 2.55 |
| Vulkan SDK | 可选（GPU 加速必需） | 见下文 |
| Python 3.10+ | 仅模型转换备选路径需要 | 本机已装 3.11 |

## 构建步骤

### 1. 准备 llama.cpp 源码（已随项目就绪）

源码位于 `third_party/llama.cpp`（b10217，含 Qwen3-ASR 的 mtmd 支持）。

如需重新克隆：

```bash
cd LiveSub/third_party
git clone --depth 1 --branch b10217 https://github.com/ggml-org/llama.cpp.git
```

### 2. 准备模型

```powershell
.\scripts\prepare_model.ps1
```

下载 llama.cpp 官方组织转换的 GGUF（Q8_0 主模型 2.2GB + BF16 音频编码器 0.64GB）。
网络受限时可用镜像，或使用本地转换备选路径（见脚本 `-Convert`）。

### 3. 构建

**CPU 版**（可立即运行，36 线程 Xeon 实测可用）：

```powershell
.\scripts\build.ps1
```

**Vulkan GPU 版（推荐，RX 7800 XT）** —— 两种方式任选：

**方式 A：链接官方预编译 DLL（免装 SDK，推荐）**

```powershell
.\scripts\build.ps1 -PrebuiltVulkan
```

脚本自动下载 llama.cpp 官方 win-vulkan 预编译包（llama.dll / mtmd.dll / ggml-vulkan.dll），
无需安装 Vulkan SDK。首次构建后 GPU 加速即生效。

**方式 B：源码编译 Vulkan（需要 Vulkan SDK）**

1. 安装 Vulkan SDK：https://vulkan.lunarg.com/sdk/home#windows
2. `.\scripts\build.ps1 -Vulkan`

### 4. 运行

```powershell
.\scripts\run.ps1
```

## 手动构建（命令行）

```bash
export PATH="/o/llvm-mingw/mingw64/bin:$PATH"   # 本机 MinGW
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release [-DGGML_VULKAN=ON]
cmake --build build -j 20
```

## 常见构建问题

| 现象 | 解决 |
|---|---|
| `_WIN32_WINNT` / `CreateFile2` 未定义 | CMake 已自动加 `-D_WIN32_WINNT=0x0A00`（MinGW） |
| Vulkan 报 `find_package(Vulkan ... glslc)` 失败 | 未安装 Vulkan SDK，或装完未重开终端 |
| 运行报缺 DLL（libstdc++-6.dll 等） | 把 `O:\llvm-mingw\mingw64\bin` 加入 PATH（run.ps1 已处理） |
| 模型加载 OOM | `gpu_layers` 调小（如 20），或换 CPU |
