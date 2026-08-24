# LiveSub

> 类型：自有原创
> 本地路径：`O:\clawwork\chuansongmen\LiveSub`
> 远程 URL：`https://github.com/Thatgfsj/LiveSub.git` ⚠ **远程 URL 内嵌明文 token（已脱敏，仅标记存在性）**
> 最近活动：2026-08-03
> 活跃度：🟢 活跃
> 与传送门2相关性：直接相关

## 1. 一句话用途
直播实时字幕软件：麦克风或系统音频经本地 Qwen3-ASR（llama.cpp 推理）转写，叠加到 OBS 窗口捕获；端到端延迟 < 2 s，纯本地，无云端 API。C++20 + CMake + MinGW-w64/MSVC 双编译器，输出 `LiveSub-Setup.exe` 安装包。

## 2. 类型细节
- 原创，作者本人。
- 无 upstream，无 fork 关系。

## 3. 技术栈
- 主语言：C++20（CMake 构建）
- 包管理：CMake + 自带 `scripts/`（下载 llama.cpp / 模型用）
- 入口文件：`src/main.cpp`，UI 入口在 `src/ui/`
- 关键依赖：
  - llama.cpp（b10217，`libmtmd` 多模态）
  - Qwen3-ASR-1.7B（GGUF，主模型 Q8_0 + 音频编码器 BF16；运行时下载，仓库不含权重）
  - WASAPI（事件驱动共享模式音频采集）
  - Direct2D + DirectWrite（分层透明字幕窗口）
  - NSIS（安装包打包，GitHub 主页语言显示 NSIS）

## 4. 规模
- 文件数（不含 `build*/` / `dist/` / `third_party/` / `models/`）：≈ 30
- 代码行数近似：C++ 估计 4k-6k（含 UI 子模块）
- 顶层结构：
  ```
  LiveSub/
  ├─ src/
  │   ├─ main.cpp  app.cpp/h  config.cpp/h  livesub.rc
  │   ├─ audio/  asr/  input/  output/  tools/  ui/
  ├─ docs/  icons/  scripts/  testdata/  tests/  tools/
  ├─ cmake/  third_party/
  ├─ CMakeLists.txt (6.7 KB)
  ├─ build/  build-mdl/  build-video/  build-vk/  dist/
  ├─ models/        # 运行时下载位置
  ├─ livesub.log (106 KB)  subtitles.txt
  └─ LICENSE (MIT)
  ```

## 5. Git 状态
- 当前分支：`main`（默认）
- 最近 3 条提交：
  - `d1b8004` 2026-08-03 显示优化：interim 与已确认文本同色（白色实色，去灰字模糊）；新段开始清空上一句
  - `756e02e` 2026-08-03 设置布局修复：整体透明度输入框收窄不侵入列 2；描边一行四件套
  - `2b45f71` 2026-08-03 移除误提交的临时文件
- 与远程：`main...origin/main`（同步）
- 未提交改动：**7 个文件 modified**（本地有未提交改动）：
  - `src/app.cpp` (+22/-10)
  - `src/app.h` (+12/-2)
  - `src/config.cpp` (+3)
  - `src/main.cpp` (+7)
  - `src/ui/settings_window.cpp` (+7/-2)
  - `src/ui/subtitle_window.cpp` (+33/-3)
  - `src/ui/subtitle_window.h` (+2)
- 未跟踪文件：未列举（提交暂存区为空）

## 6. 工程化程度
- [x] README（19.4 KB，包含完整开发提示词，可被其他 AI Agent 复现整个项目）
- [x] LICENSE（MIT）
- [ ] .github/workflows（API 未返回 `.github` 目录，需核实）
- [x] 测试（`tests/`、`testdata/`）
- [ ] 依赖锁定（CMake 无对应 lockfile；模型权重运行时下载）

## 7. 安全隐患
- **远程 URL token 泄露：是 ⚠** —— `git remote -v` 显示 remote URL 含 `https://Thatgfsj:<token>@github.com/Thatgfsj/LiveSub.git`，token 前缀 `ghp_`（已脱敏为「存在」标记，**未在文档中复述 token 明文**）。**强烈建议立即重置该 PAT 并清除本地 `git remote set-url origin` 改回标准 URL**。该 token 来自 `GITHUB_TOKEN` 环境变量或用户登录凭据，如该 token 失效将影响后续 `git push`。
- `.env` / 密钥文件存在性：未在仓库根检测到 `.env` / `.keys` / `id_rsa` / `*.pem`。
- 其他：仓库根含 `livesub.log`（106 KB）+ `subtitles.txt` + 多个 `build*` 目录未忽略（`.gitignore` 仅 328 字节，可能不全），运行时日志若包含用户音频内容需评估隐私风险。

## 8. 与传送门2相关性
- **直接相关**：LiveSub 是「传送门」核心工具之一（实时字幕，与 MiniMax ASR/TTS/直播工作流直接挂钩），README 已自述「纯本地 Qwen3-ASR + llama.cpp」与 TTS/直播链路配合。GitHub 仓库语言标记 NSIS（说明安装包工程占比高）。

## 9. 备注
- 「传送门」原始 prompt 列出 LiveSub 为核心子项目，已确认在 `O:\clawwork\chuansongmen\LiveSub\` 内嵌维护（非独立仓库根）。
- 本地 7 个 modified 文件未提交（可能为调试中或断点续作），建议在「传送门2」迁移前先 `git stash` 或 `commit`。
- README 中包含「复制给 AI Agent 即可复现本项目」的完整提示词，**对「传送门2」重建字幕子系统是高价值参考**。
- 仓库有大量构建产物（`build/`、`build-mdl/`、`build-video/`、`build-vk/`、`dist/`、`models/`），**未充分 ignore**。

## 10. 原始证据（可折叠）
<details>
<summary>展开命令输出</summary>

```
gh api repos/Thatgfsj/LiveSub
{
  "size": 39009, "language": "NSIS", "license": "MIT",
  "stargazers_count": 1, "default_branch": "main",
  "created_at": "2026-08-01T12:17:47Z",
  "pushed_at": "2026-08-03T07:20:26Z",
  "description": "直播实时语音字幕（本地 Qwen3-ASR + llama.cpp），麦克风实时转字幕，OBS 窗口捕获"
}

git remote -v
origin  https://Thatgfsj:ghp_****@github.com/Thatgfsj/LiveSub.git (fetch)
origin  https://Thatgfsj:ghp_****@github.com/Thatgfsj/LiveSub.git (push)
       ^^^^^^^^^^^^^^^^^^ token 实际值已脱敏，绝不外泄

git status --porcelain | wc -l
7
git diff --stat
 src/app.cpp                | 32 ++++++++++++++++++++++-----------
 src/app.h                  | 14 ++++++++++++--
 src/config.cpp             |  3 +++
 src/main.cpp               |  7 +++++++
 src/ui/settings_window.cpp |  9 +++++++--
 src/ui/subtitle_window.cpp | 36 +++++++++++++++++++++++++++++++++---
 src/ui/subtitle_window.h   |  2 ++

git log --oneline -3
d1b8004 显示优化：interim 与已确认文本同色（白色实色，去灰字模糊）
756e02e 设置布局修复：整体透明度输入框收窄不侵入列2；描边一行四件套
2b45f71 移除误提交的临时文件
```

</details>