# LiveSub — 直播实时字幕（本地 Qwen3-ASR）

说话，屏幕上出字幕。麦克风或电脑声音实时转成字幕，显示在透明置顶窗口里，OBS 窗口捕获直接叠进直播画面。全程本地推理，不碰云端。

## 功能

- **实时字幕**：VAD 分段驱动，说话约 1 秒出首版字幕，之后平滑更新，停顿定稿，延迟 < 2 秒
- **双音源**：麦克风（直播讲解）和电脑声音（看视频/直播时给内容配字幕），共用同一个字幕窗口
- **语音输入**：托盘开启后，定稿句直接打进当前焦点窗口（听写模式），不动剪贴板
- **讲话稿记录**：托盘开始/结束，定稿内容实时写到桌面 `讲话记录_年月日_时分秒.txt`
- **字幕样式**：字号、颜色、背景透明度、描边（0-3 粗细，0=关）、位置（像素中心坐标）、行数，设置里都能改
- **托盘**：状态灯（蓝=就绪 绿=识别中 红=出错），右键菜单：设置 / 显示隐藏 / 字幕开关 / 语音输入 / 记录 / 退出
- **模型管理**：设置里切换大/小模型，缺文件会提示并打开下载器（自动选对应大小），下载器支持双镜像、断点续传、断线自动重试
- **中英日韩**：自动检测语言

## 直接使用

从 [Releases](https://github.com/Thatgfsj/LiveSub/releases) 下载 `LiveSub-Setup.exe` 安装。安装完会自动打开模型下载器，选大模型（1.7B，推荐）或小模型（0.6B），下完启动就能用。

OBS 用法：来源 → 窗口捕获 → 选 "LiveSub 字幕"（OBS 31+ 可勾选"允许透明度"保留透明背景）。

## 基于什么

| 组件 | 说明 |
|---|---|
| 模型 | [Qwen3-ASR-1.7B](https://huggingface.co/Qwen/Qwen3-ASR-1.7B)（GGUF：`ggml-org/Qwen3-ASR-1.7B-GGUF`，主模型 Q8_0 + 音频编码器 **BF16**） |
| 推理引擎 | [llama.cpp](https://github.com/ggml-org/llama.cpp)（b10217，`libmtmd` 多模态库，Vulkan/CPU 双后端） |
| 音频采集 | Windows WASAPI（事件驱动共享模式） |
| 字幕渲染 | Direct2D + DirectWrite（分层透明窗口） |
| 语言 | C++20，CMake（MinGW-w64 / MSVC 均可） |

> 说明：Qwen3-ASR 开源权重为 0.6B / 1.7B（"Flash" 是阿里云云端 API 版本，本地用的是开源权重）；
> 权重归 Qwen 团队所有（Apache-2.0），**模型文件不包含在本仓库**，安装后由下载器获取。

## 开发提示词（复制给 AI Agent 即可复现本项目）

> 把下面整段复制给任意编程 Agent（Claude 等）。**方案由 Agent 自行调研选择**，
> 本项目（llama.cpp + Qwen3-ASR）是已验证的参考实现；提示词末尾标注了
> "参考实现细节（已验证）"，供对照或直接采用。详细踩坑记录见下文
> 《开发经验与踩坑记录》一节。

---
```
## 开发提示词（复制给 AI Agent 即可复现本项目）

> 把下面整段复制给任意编程 Agent（Claude 等）。**方案由 Agent 自行调研选择**，
> 本项目（llama.cpp + Qwen3-ASR）是已验证的参考实现；提示词末尾标注了
> "参考实现细节（已验证）"，供对照或直接采用。详细踩坑记录见下文
> 《开发经验与踩坑记录》一节。

```
```
**【任务】** 为 Windows 11 开发一个直播实时字幕软件：麦克风语音 → 本地 ASR →
透明置顶字幕窗口（OBS 窗口捕获），**端到端延迟 < 2 秒**，纯本地运行（无云端 API），
支持中文与英文（自动检测）。附带：系统托盘（状态指示 + 菜单：设置/隐藏字幕/记录讲话稿/退出）、
讲话稿记录（定稿句实时写桌面 `讲话记录_年月日_时分秒.txt`）、运行日志文件。

**【目标与约束（方案由你决定）】**
- 端到端延迟 < 2s（说话 → 字幕上屏）；纯本地；OBS 可捕获；长时间运行稳定
- **技术栈自行调研选择**，可从以下维度评估后决定：
  - 推理引擎：llama.cpp（含多模态）/ whisper.cpp / sherpa-onnx / FunASR / ONNX Runtime 等
  - 模型：Qwen3-ASR-0.6B/1.7B、Whisper 系列、SenseVoice 等（注意：本地可用的是开源权重，
    "Flash" 等云端版本不可用；音频编码器类组件保持全精度不要量化）
  - GPU 后端：Windows + AMD 显卡优先 Vulkan / DirectML（无需装 CUDA）；
    CPU 后端可作降级（36 线程实测单窗口 1-2s，勉强可用）
  - 语言/框架：C++ 或 Rust 均可（若选 C++：CMake + MinGW-w64 或 MSVC 皆可）
  - 界面：Win32 / Direct2D / Qt 等，需要能实现"透明置顶窗口 + 托盘 + 设置窗"
- 若选择与参考实现不同的方案，请先说明选型理由（延迟/质量/硬件适配/部署复杂度）

**【架构（通用）】** 采集线程（麦克风→重采样 16k→VAD）→ 缓冲队列 → ASR 线程
（分段窗口→转写→文本合并）→ UI 线程（字幕窗 + 托盘 + 设置窗）。日志记录每个
窗口的识别文本与耗时（便于定位问题）。

**【关键实现要点（通用工程经验，与技术栈无关，务必遵守）】**
1. **流式识别必须"VAD 分段驱动"**：固定时间滑动窗口必然跨句，识别出"旧句尾+新句头"
   混合内容，显示层无论如何处理都掩盖不干净。正确做法：VAD 检测到语音段起点时记录
   累计样本数，识别窗口 = [段起点, 当前]，从句子开头对齐，逐秒增长完善；段结束（静音）
   时用**固定的段尾样本数**定稿（不要在 ASR 处理时再取"当前"，否则用户停顿后又说新句
   会导致定稿窗口漂移、新句被提前定稿）。
2. 环形缓冲：写满后**必须覆盖最旧数据且累计计数器持续增长**（否则 ASR 线程的条件变量
   永久阻塞，约 12 秒后字幕卡死）；取窗口 = 从缓冲末尾往回取最新 N 秒。
3. ASR 线程等待加短超时（音频停止后段结束的定稿请求才能被处理），数据增量不足一个
   更新步长时跳过（防重复识别）；**新段首窗等满 1.0s 再识别**（太短窗口识别碎片闪现）；
   **新段开始时清空历史**（停顿后旧句从字幕消失）。
4. VAD：帧能量下限 clamp（数字静音会把底噪估计拉崩导致阈值失效）；语音门限用手动值
   （如 -52dB）或自适应（底噪+余量）；语音持续数百 ms 才确认段开始；窗口级"活跃帧比例"
   判断比平均能量鲁棒。
5. 幻觉过滤：短语气词（"嗯。""啊。"等）不进字幕；与刚定稿句完全相同的结果去重。
   **不要做子串/前缀重叠等更强的显示层过滤**——会误杀正常新句（"大家好" vs "大家好，我是主持人"）。
6. 透明字幕窗：普通窗口渲染 alpha 不生效（黑底）。正确做法：分层窗口 + 内存 DIB
   （32bpp 预乘 alpha）+ 每帧上传 + 整体 alpha 控制（可做淡入淡出）。OBS 用
   WGC（Windows Graphics Capture）捕获可保留透明度。
7. 字幕最多 2 行；长文本逐级缩小字号（避免"第二行孤字"），仍超行才滚动保留最后 2 行。
8. GPU 首次调用要编译 shader（1-3s）→ 启动时用 1 秒静音预热。
9. 设置窗"应用"时重建队列**必须先停止 ASR 线程**（悬空条件变量 → 线程挂起）；
   退出流程幂等。
10. 中文文件路径（讲话记录等）用宽字符文件 API（不要用按 ANSI 解释路径的流库）。

**【参考实现细节（已验证；若选 llama.cpp + Qwen3-ASR 可直接采用）】**
- 模型：`ggml-org/Qwen3-ASR-1.7B-GGUF`（主模型 Q8_0 + mmproj **BF16**，mmproj 勿量化）
- mtmd 音频流程：`mtmd_bitmap_init_from_audio` → `mtmd_tokenize`
  （prompt：`<|im_start|>user
<__media__>Transcribe the audio.<|im_end|>
<|im_start|>assistant
`，
  marker 自动替换为 `<|audio_start|>`…`<|audio_end|>`）→ 音频 chunk 走
  `mtmd_batch_init/add_chunk/encode/get_output_embd`（不要用单 chunk encode）→
  `mtmd_helper_decode_image_chunk`；文本 chunk 走 `mtmd_helper_eval_chunk_single`；最后自回归采样。
- 采样循环手动构建 batch（新版 `llama_batch_get_one` 的 pos/logits 是 nullptr）：
  `llama_batch_init(1,0,1)` 后**必须设 `batch.n_tokens = 1`**；每次转写前清 KV 缓存。
- GPU 加速可链接 llama.cpp 官方 release 的 win-vulkan 预编译 DLL（免装 Vulkan SDK），
  运行时复制全部 ggml-cpu-* 变体 DLL + OpenMP 运行时 + 编译工具链运行时 DLL。
- 工程细节：MinGW 构建需定义 `_WIN32_WINNT=0x0A00`；PowerShell 脚本保存为 UTF-8 BOM。

**【验收标准】**
- 中文/英文口播识别正确（自动语言检测），识别结果与语音内容一致
- 说话 → 约 1.0s 出首版字幕，之后每 0.8s 平滑完善；停顿 0.8s 定稿；停顿后说新句，旧句消失、新句从头渐进
- 连续说话 60 秒以上不卡死、不重复、不闪回旧内容；纯静音零识别
- OBS 窗口捕获正常（WGC + 允许透明度）；托盘状态与菜单齐全；讲话稿记录到桌面
- 日志记录每个窗口的识别文本与耗时

```
---

## 特性

- **实时流式识别**：VAD 分段驱动，窗口从句子开头对齐，Local Agreement 前缀确认（文本只增不减不回退），停顿定稿
- **语音输入**：托盘开启/关闭，定稿句直接输入当前焦点窗口（SendInput Unicode 注入，不动剪贴板）
- **系统托盘**：状态图标（蓝=就绪 / 绿=识别中 / 红=出错），右键菜单：设置 / 显示隐藏字幕 / 字幕开关 / 语音输入 / 记录讲话稿 / 退出
- **讲话稿记录**：托盘开始/结束，定稿内容实时写入桌面 `讲话记录_年月日_时分秒.txt`
- **OBS 集成**：透明置顶字幕窗（窗口捕获，WGC 可保留透明度）；另支持 SRT/文本输出（按时间命名）
- **多语言**：中/英/日/韩自动检测
- **可配置**：位置（像素中心坐标）、字号/颜色/背景透明度、描边（0-3，0=关）、VAD 阈值、字幕行数、更新步长等
- **模型管理**：设置里切换大/小模型（缺文件提示并打开下载器自动下载），下载器双镜像 + 断点续传 + 断线重试

## 快速开始（Windows 11）

```powershell
# 1. 获取 llama.cpp 源码（构建依赖）
git clone --depth 1 --branch b10217 https://github.com/ggml-org/llama.cpp.git third_party/llama.cpp

# 2. 下载模型（约 2.9GB，仅一次）——模型不放仓库
.\scripts\prepare_model.ps1

# 3. 构建（GPU 版：链接 llama.cpp 官方预编译 Vulkan DLL，免装 SDK）
.\scripts\build.ps1 -PrebuiltVulkan
#    或 CPU 版： .\scripts\build.ps1

# 4. 运行（GPU 版优先）
.\scripts\run.ps1
```

## 架构

```
麦克风 (WASAPI 48kHz)
   ↓ 重采样 16kHz
VAD（能量双门限，自适应底噪 / 手动阈值；分段）
   ↓ 语音段起点
llama.cpp + libmtmd（Qwen3-ASR-1.7B Q8_0 + BF16 音频编码器）
   ↓ 流式文本合并（幻觉过滤 / 相同句去重 / 行数滚动）
字幕窗口（Direct2D 分层透明，2 行 + 字号自适应）
   + SRT/文本输出 + 讲话稿记录
```

## 目录结构

```
src/
├─ main.cpp            入口（--config/--settings/--wav/--list-devices）
├─ app.{h,cpp}         线程编排：采集→VAD→分段窗口→ASR→字幕
├─ config.{h,cpp}      INI 配置
├─ audio/              WASAPI 采集、重采样、WAV 读取
├─ asr/                VAD、音频队列、ASR 引擎（mtmd 封装）、文本合并
├─ ui/                 字幕窗（D2D 分层透明）、设置窗、托盘
└─ output/             SRT/文本/讲话稿记录
scripts/               构建/运行/模型/打包脚本
docs/                  构建与 OBS 集成文档
tests/                 单元与离线识别测试
testdata/              官方测试音频（asr_zh/asr_en，来自 Qwen 仓库）
```

## 开发与踩坑记录

> 下面这份记录是开发过程中真实踩过的坑与结论。**无论是继续开发、排查问题，还是让 AI 助手接手这个项目，请把这一节作为上下文**——很多问题都是反复踩坑总结出来的，照着它能省大量时间。

### 一、模型与推理引擎

1. **Qwen3-ASR 是"解码器 + 音频编码器"双文件结构**：主 GGUF（Qwen3 文本模型）+ `mmproj` GGUF（音频编码器，SenseVoice 风格 Conv2d×3 + 24 层 Transformer）。llama.cpp 通过 `libmtmd`（tools/mtmd）支持，需要两个文件同时加载。
2. **mmproj（音频编码器）必须保持 BF16 全精度**，量化会明显损失识别质量。主模型可 Q8_0（推荐，质量好）或更低量化换速度。
3. 模型转换用 llama.cpp 官方组织 `ggml-org/Qwen3-ASR-1.7B-GGUF`（与 b10217 完全匹配）。自己跑 `convert_hf_to_gguf.py` 也可，但注意版本一致。
4. **mtmd 音频调用流程**（已验证的路径）：`mtmd_bitmap_init_from_audio` → `mtmd_tokenize`（prompt 用 `<|im_start|>user\n<__media__>Transcribe the audio.<|im_end|>\n<|im_start|>assistant\n`，marker 会被替换为 `<|audio_start|>`…`<|audio_end|>` 包裹的嵌入）→ 音频 chunk 走 `mtmd_batch_init/add/encode/get_output_embd`（**不要用单 chunk 的 mtmd_encode_chunk + get_output_embd，行为不一致**）→ `mtmd_helper_decode_image_chunk` → 文本 chunk 走 `mtmd_helper_eval_chunk_single` → 最后自回归采样。
5. **`llama_batch_get_one` 新版只返回 (tokens, n_tokens)，pos/logits 都是 nullptr**——需要手动 `llama_batch_init(1,0,1)` 填 token/pos/n_seq_id/seq_id/logits，**并且必须手动设 `batch.n_tokens = 1`**（llama_batch_init 不会初始化它，不设会报 "n_tokens == 0"）。
6. 每次 transcribe 前要 `llama_memory_clear(llama_get_memory(ctx), true)` 清 KV——同一 context 多次调用必须清，否则 M-RoPE 位置检查报错（X vs Y 不一致）。
7. **GPU 首次调用要编译 shader（1~3 秒）**，会让第一句话看起来"卡死"。解决：启动时用 1 秒静音做一次预热 transcribe。
8. GPU 方案：Windows 下直接链接 **llama.cpp 官方 release 的 `llama-b*-bin-win-vulkan-x64.zip` 预编译 DLL**（llama.dll/mtmd.dll/ggml.dll/ggml-vulkan.dll），免装 Vulkan SDK。运行时需复制 `ggml-cpu-*.dll` 全部变体 + `libomp140.x86_64.dll` + MinGW 运行时（libstdc++-6.dll 等）。

### 二、流式识别的正确姿势

9. **固定时间滑动窗口必然跨句**：每 1 秒取"最近 N 秒音频"的窗口会横跨句子边界，识别出"旧句尾+新句头"的混合内容（如旧句尾"龙虾。"+新句"好的…"拼在一起）。**在显示层去重/截断/粘连处理都是掩盖，永远处理不干净**（还会误杀正常句子）。
10. **正确方案：VAD 分段驱动**。VAD 检测到语音段起点（on_speech_start 时记录累计样本数），识别窗口 = [段起点, 当前]，**从句子开头对齐**；段结束（静音）时用固定段尾定稿。窗口内容永远"从这句开头到现在"，渐进完善，无跨句混合。
11. **定稿竞态**：段结束的 finalize 请求（on_speech_end）到 ASR 线程处理之间，如果用户又说话了（新段开始），finalize 窗口起点会漂移到新段 → 新句被提前定稿+重复。修复：on_speech_end 时**记录段尾样本数**，finalize 窗口用 [段起点, 段尾] 固定区间。
12. **音频停止后定稿丢失**：ASR 线程阻塞在队列条件变量上等新数据，段结束请求永远不被处理（最后一句不定稿）。修复：等待加 200ms 超时 + 数据增量不足一个 hop 时跳过（避免重复识别相同窗口）。
13. **新段首窗太短会碎片闪现**（0.5s 音频识别出"文。"之类）→ 新段首窗等满 1.0s 再识别（whisper_streaming 默认 min-chunk-size 也是 1.0s），首版字幕直接较完整。
14. **新段开始清空历史**：停顿后再说新句，旧句应从字幕消失（VAD 分段本身就是"新句子"的信号）。
15. VAD：**数字静音（0 值）会把底噪估计拉到 -240dB 导致阈值失效** → 帧能量下限 clamp 到 -60dB。语音门限可用手动阈值（-52dB 之类）或自适应（底噪+余量）。
16. 幻觉输出（"嗯。"、"啊。"）过滤：≤2 字的语气词不进字幕；窗口级"活跃帧比例"（超过阈值的 20ms 帧占比 <15% 视为静音）比平均能量鲁棒。

### 三、工程与 UI 坑

17. **MinGW 构建 llama.cpp**：cpp-httplib 需要 `-D_WIN32_WINNT=0x0A00`（MinGW 默认 Win7 级别，报 "doesn't support Windows 8 or lower" / CreateFile2 未声明）。
18. **MinGW 的 std::ofstream 用 ANSI 代码页解释路径**：UTF-8 中文文件名会变成乱码（"讲话记录.txt" → GBK 乱码名）。用 `_wfopen`（UTF-8→UTF-16 再打开）。
19. **PowerShell 5.1 读取 .ps1 需要 UTF-8 BOM**，否则中文乱码/语法错。
20. **Git Bash（MSYS）的 heredoc 会破坏反斜杠**：`<<'EOF'` 里写 `\\` 会被转义成 `\`，`\n` 变真实换行，`\b` 变退格——**含反斜杠/转义的补丁一律写成 .py 文件再执行**（Write 工具不转义）。
21. **D2D 透明窗口**：HwndRenderTarget 渲到普通窗口 alpha 不生效（黑底）。正确做法：`WS_EX_LAYERED` + 内存 DIB（32bpp 预乘 alpha）+ D2D **DCRenderTarget** 画到 DIB + `UpdateLayeredWindow`（`BLENDFUNCTION.SourceConstantAlpha` 可做整体淡入淡出动画）。
22. **字幕"孤字"**：软换行时最后一行只剩 1 个字很难看 → 行数超限时用 `IDWriteTextLayout::SetFontSize` 逐级缩小字号（布局级覆盖，不重建 TextFormat），最多缩 50%，仍超行才滚动截断。
23. **异步 set_text 的 use-after-free**：ASR 线程 exchange 新指针并 delete 旧指针，渲染线程可能正持有旧指针 → 用互斥锁保护或延迟释放。
24. **设置窗"应用"时重建队列**：ASR 线程可能阻塞在旧队列的 condition_variable 上，直接 delete 队列 = 悬空引用（线程挂起/崩溃）→ **先 join ASR 线程再重建再重启**。
25. **幂等 shutdown**：析构也会调用 shutdown，二次调用要幂等（标志位）。
26. **环形缓冲写满必须覆盖旧数据且 total 持续增长**，否则等待条件永远不满足（"12 秒后字幕卡死"）。窗口取"最新 N 秒"要从 head_ 往回取，不是从缓冲起点。
27. OBS 捕获：分层窗口用 **WGC（Windows Graphics Capture）** 方式捕获，OBS 31+ 可勾选"允许透明度"保留 alpha；老版本会显示黑底，用半透明背景即可。

### 四、性能参考

| 平台 | 单窗口计算（6s 窗口） |
|---|---|
| 中高端独立显卡（Vulkan 后端，Q8_0） | ~0.25s（enc ~35ms + dec ~100ms） |
| 多核 CPU（36 线程） | ~1.2-1.8s |

显存占用约 3GB。端到端延迟（GPU）：VAD 触发后 1.0s 出首版字幕，之后每 0.8s 平滑更新，停顿 0.8s 定稿（首窗 1.0s 对齐 whisper_streaming 默认 min-chunk-size）。

## 常见问题

- **启动报"音频客户端初始化失败"**：检查 Windows 麦克风权限（设置 → 隐私 → 麦克风）
- **字幕不更新**：查看 `livesub.log`（每个窗口的识别内容与耗时都在里面），托盘变红右键看提示
- **OBS 捕获黑底/找不到窗口**：见 `docs/OBS_SETUP.md`
- **速度慢**：确认在用 GPU 版（`build-vk/` 或打包版），CPU 版单窗口 1-2 秒属正常

## 许可

- 本仓库代码：MIT
- Qwen3-ASR 模型权重：Apache-2.0（归 Qwen 团队，需自行下载）
- llama.cpp：MIT
