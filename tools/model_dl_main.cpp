// model-dl.exe：模型下载器（安装器调用 / 主程序首启兜底）
// 用法: model-dl.exe [--auto] [--large|--small|--sensevoice|--fast]
// 四种模型：
//   精准 大模型（Qwen3-1.7B，llama.cpp）   约 2.7GB
//   轻量 小模型（Qwen3-0.6B，llama.cpp）   约 1.1GB
//   均衡 SenseVoice（int8，sherpa-onnx）   约 230MB，中文最准（推荐）
//   极速 流式zipformer（int8，sherpa-onnx）约 190MB，最快
#include <cstdio>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>

#include <windows.h>
#include <commctrl.h>

#include "tools/model_downloader.h"

// UTF-8 → UTF-16（错误信息显示）
static std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w((size_t)n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

// 仓库前缀（URL 直接拼远端文件名）
static const char* kLargeMir = "https://hf-mirror.com/ggml-org/Qwen3-ASR-1.7B-GGUF/resolve/main/";
static const char* kLargeHf  = "https://huggingface.co/ggml-org/Qwen3-ASR-1.7B-GGUF/resolve/main/";
static const char* kSmallMir = "https://hf-mirror.com/ggml-org/Qwen3-ASR-0.6B-GGUF/resolve/main/";
static const char* kSmallHf  = "https://huggingface.co/ggml-org/Qwen3-ASR-0.6B-GGUF/resolve/main/";
static const char* kSvMir    = "https://hf-mirror.com/csukuangfj/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/resolve/main/";
static const char* kSvHf     = "https://huggingface.co/csukuangfj/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/resolve/main/";
static const char* kZiMir    = "https://hf-mirror.com/csukuangfj/sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20/resolve/main/";
static const char* kZiHf     = "https://huggingface.co/csukuangfj/sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20/resolve/main/";

struct FileSpec {
    const char* rel;      // 相对 model 目录的保存路径（含子目录）
    const char* mir;      // 主源仓库前缀
    const char* hf;       // 备源仓库前缀
    const char* remote;   // 远端文件名
    uint64_t size;
    const wchar_t* name;  // 进度显示名
};

enum ModelId { M_LARGE = 0, M_SMALL = 1, M_SENSEVOICE = 2, M_FAST = 3 };

// 各模型的文件清单（子目录与 config/check_model_files 的约定一致）
static FileSpec g_files_large[] = {
    { "Qwen3-ASR-1.7B-Q8_0.gguf",        kLargeMir, kLargeHf, "Qwen3-ASR-1.7B-Q8_0.gguf",        2165034944ull, L"主模型" },
    { "mmproj-Qwen3-ASR-1.7B-bf16.gguf", kLargeMir, kLargeHf, "mmproj-Qwen3-ASR-1.7B-bf16.gguf", 641773984ull,  L"音频编码器" },
};
static FileSpec g_files_small[] = {
    { "Qwen3-ASR-0.6B-Q8_0.gguf",        kSmallMir, kSmallHf, "Qwen3-ASR-0.6B-Q8_0.gguf",        804749248ull, L"主模型" },
    { "mmproj-Qwen3-ASR-0.6B-bf16.gguf", kSmallMir, kSmallHf, "mmproj-Qwen3-ASR-0.6B-bf16.gguf", 378575520ull, L"音频编码器" },
};
static FileSpec g_files_sv[] = {
    { "sensevoice\\model.int8.onnx", kSvMir, kSvHf, "model.int8.onnx", 239233841ull, L"SenseVoice 模型" },
    { "sensevoice\\tokens.txt",      kSvMir, kSvHf, "tokens.txt",      10000ull,     L"词表" },
};
static FileSpec g_files_fast[] = {
    { "fast\\encoder-epoch-99-avg-1.int8.onnx", kZiMir, kZiHf, "encoder-epoch-99-avg-1.int8.onnx", 181895032ull, L"编码器" },
    { "fast\\decoder-epoch-99-avg-1.int8.onnx", kZiMir, kZiHf, "decoder-epoch-99-avg-1.int8.onnx", 13091040ull,  L"解码器" },
    { "fast\\joiner-epoch-99-avg-1.int8.onnx",  kZiMir, kZiHf, "joiner-epoch-99-avg-1.int8.onnx",  3228404ull,   L"连接器" },
    { "fast\\tokens.txt",                       kZiMir, kZiHf, "tokens.txt",                       10000ull,     L"词表" },
};

static HWND g_progress = nullptr, g_status = nullptr, g_done = nullptr;
static std::atomic<bool> g_cancel{false};
static int g_choice = M_LARGE;
static bool g_auto_mode = false;  // --auto 命令行模式：大小以命令行参数为准，不读 UI

static void set_status(const std::wstring& s) {
    if (g_status) SetWindowTextW(g_status, s.c_str());
}

static void download_worker(const std::string& model_dir, ModelId id) {
    FileSpec* files = id == M_LARGE ? g_files_large :
                      id == M_SMALL ? g_files_small :
                      id == M_SENSEVOICE ? g_files_sv : g_files_fast;
    const int n = id == M_SENSEVOICE ? 2 : id == M_FAST ? 4 : 2;

    // 目标文件已存在且完整 → 直接提示，不重复下载
    {
        auto size_ok = [](const std::string& p, uint64_t expect) {
            FILE* f = fopen(p.c_str(), "rb");
            if (!f) return false;
            _fseeki64(f, 0, SEEK_END);
            const long long sz = _ftelli64(f);
            fclose(f);
            return sz >= (long long)expect;
        };
        bool all = true;
        for (int i = 0; i < n; i++) {
            if (!size_ok(model_dir + "\\" + files[i].rel, files[i].size)) { all = false; break; }
        }
        if (all) {
            set_status(L"模型文件已存在且完整，无需下载");
            EnableWindow(g_done, TRUE);
            SetFocus(g_done);
            return;
        }
    }

    bool all_ok = true;
    std::string last_err;
    for (int i = 0; i < n && all_ok; i++) {
        // 子目录（sensevoice/fast）不存在则创建
        {
            std::string rel = files[i].rel;
            const size_t slash = rel.find_last_of("\\/");
            if (slash != std::string::npos) {
                CreateDirectoryA((model_dir + "\\" + rel.substr(0, slash)).c_str(), nullptr);
            }
        }
        DownloadFile f{ std::string(files[i].mir) + files[i].remote,
                        std::string(files[i].hf) + files[i].remote,
                        model_dir + "\\" + files[i].rel, files[i].size };
        set_status(std::wstring(L"正在下载 ") + files[i].name + L" ...");
        int rc = 0;
        for (int retry = 0; retry < 3 && !g_cancel; retry++) {
            std::string dl_err;
            rc = download_file(f,
                [](uint64_t done, uint64_t total) -> bool {
                    if (g_cancel) return false;
                    // 进度节流：每 256KB 更新一次
                    static uint64_t last = 0;
                    if (done - last < 256 * 1024) return true;
                    last = done;
                    if (total > 0) {
                        SendMessageW(g_progress, PBM_SETPOS, (WPARAM)(done * 100 / total), 0);
                        wchar_t buf[128];
                        swprintf(buf, 128, L"%.0f / %.0f MB", done / 1048576.0, total / 1048576.0);
                        SetWindowTextW(g_status, buf);
                    } else {
                        wchar_t buf[128];
                        swprintf(buf, 128, L"已下载 %.0f MB ...", done / 1048576.0);
                        SetWindowTextW(g_status, buf);
                    }
                    return true;
                }, &dl_err);
            if (rc == 0) break;
            last_err = dl_err.empty() ? "网络中断" : dl_err;
            // 退避重试：2s / 4s（下载器内部已对两个镜像各重试多次）
            std::this_thread::sleep_for(std::chrono::seconds(2 * (retry + 1)));
        }
        if (rc != 0) all_ok = false;
        else SendMessageW(g_progress, PBM_SETPOS, 100, 0);
    }

    set_status(all_ok ? L"模型下载完成！可以关闭本窗口"
                      : L"下载失败：" + utf8_to_wide(last_err) + L"  （请检查网络后重新运行）");
    EnableWindow(g_done, TRUE);
    if (all_ok) SetFocus(g_done);
}

// 解析 model 目录（exe 所在目录 + model）
static std::string get_model_dir() {
    std::string model_dir;
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring w(buf);
    size_t pos = w.find_last_of(L"\\/");
    if (pos != std::wstring::npos) w.resize(pos);
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    model_dir.assign((size_t)n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, model_dir.data(), n, nullptr, nullptr);
    model_dir += "\\model";
    return model_dir;
}

static void begin_download(HWND hwnd) {
    EnableWindow(GetDlgItem(hwnd, 1), FALSE);
    EnableWindow(GetDlgItem(hwnd, 2), FALSE);
    EnableWindow(GetDlgItem(hwnd, 3), FALSE);
    EnableWindow(GetDlgItem(hwnd, 5), FALSE);
    EnableWindow(GetDlgItem(hwnd, 6), FALSE);
    // --auto 模式：以命令行指定的模型为准（不被 UI 默认勾选覆盖）
    if (!g_auto_mode) {
        if (SendMessageW(GetDlgItem(hwnd, 2), BM_GETCHECK, 0, 0) == BST_CHECKED) g_choice = M_LARGE;
        else if (SendMessageW(GetDlgItem(hwnd, 3), BM_GETCHECK, 0, 0) == BST_CHECKED) g_choice = M_SMALL;
        else if (SendMessageW(GetDlgItem(hwnd, 5), BM_GETCHECK, 0, 0) == BST_CHECKED) g_choice = M_SENSEVOICE;
        else g_choice = M_FAST;
    }
    std::thread(download_worker, get_model_dir(), (ModelId)g_choice).detach();
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR lpCmdLine, int) {
    // 命令行：--auto [large|small|sensevoice|fast] 直接开始下载（跳过选择界面）
    bool auto_start = false;
    {
        std::wstring cmd = lpCmdLine ? lpCmdLine : L"";
        if (cmd.find(L"--auto") != std::wstring::npos) { auto_start = true; g_auto_mode = true; }
        if (cmd.find(L"--small") != std::wstring::npos) g_choice = M_SMALL;
        if (cmd.find(L"--large") != std::wstring::npos) g_choice = M_LARGE;
        if (cmd.find(L"--sensevoice") != std::wstring::npos) g_choice = M_SENSEVOICE;
        if (cmd.find(L"--fast") != std::wstring::npos) g_choice = M_FAST;
    }
    const std::string model_dir = get_model_dir();
    CreateDirectoryA(model_dir.c_str(), nullptr);

    // 窗口：选择模型 + 进度
    const wchar_t* cls = L"LiveSubModelDl";
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.hInstance = hInst;
    wc.lpfnWndProc = [](HWND h, UINT m, WPARAM wp, LPARAM lp) -> LRESULT {
        if (m == WM_COMMAND) {
            const int id = LOWORD(wp);
            if (id == 1) { begin_download(h); return 0; }
            if (id == 4) { PostQuitMessage(0); return 0; }
            return 0;
        }
        if (m == WM_CLOSE) { g_cancel = true; DestroyWindow(h); return 0; }
        if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
        return DefWindowProcW(h, m, wp, lp);
    };
    wc.lpszClassName = cls;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, cls, L"LiveSub 模型下载",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 540, 270, nullptr, nullptr, hInst, nullptr);
    CreateWindowExW(0, L"STATIC", L"选择要下载的模型：",
        WS_CHILD | WS_VISIBLE, 20, 12, 480, 20, hwnd, nullptr, hInst, nullptr);
    struct RadioDef { int id; const wchar_t* text; int y; };
    const RadioDef radios[] = {
        { 5, L"均衡 SenseVoice：中文最准，约 230MB（推荐）", 36 },
        { 6, L"极速 流式zipformer：最快、延迟最低，中英双语，约 190MB", 60 },
        { 3, L"轻量 Qwen3-0.6B：本地大模型，约 1.1GB", 84 },
    };
    for (const auto& r : radios) {
        CreateWindowExW(0, L"BUTTON", r.text,
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | BS_LEFT,
            20, r.y, 490, 22, hwnd, (HMENU)(INT_PTR)r.id, hInst, nullptr);
    }
    // 精准大模型 radio（ID=2）
    HWND r_big = CreateWindowExW(0, L"BUTTON", L"精准 Qwen3-1.7B：效果最好，约 2.7GB",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | BS_LEFT,
        20, 108, 490, 22, hwnd, (HMENU)2, hInst, nullptr);
    // 默认勾选均衡 SenseVoice（--auto 指定时勾对应项）
    HWND def_check = g_choice == M_LARGE ? r_big :
                     g_choice == M_SMALL ? GetDlgItem(hwnd, 3) :
                     g_choice == M_SENSEVOICE ? GetDlgItem(hwnd, 5) : GetDlgItem(hwnd, 6);
    SendMessageW(def_check, BM_SETCHECK, BST_CHECKED, 0);

    g_progress = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE,
                                 20, 140, 490, 24, hwnd, nullptr, hInst, nullptr);
    SendMessageW(g_progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    g_status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                               20, 170, 490, 20, hwnd, nullptr, hInst, nullptr);
    CreateWindowExW(0, L"BUTTON", L"开始下载", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    330, 200, 90, 26, hwnd, (HMENU)1, hInst, nullptr);
    g_done = CreateWindowExW(0, L"BUTTON", L"关闭", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                             430, 200, 70, 26, hwnd, (HMENU)4, hInst, nullptr);
    ShowWindow(hwnd, SW_SHOW);
    if (auto_start) {
        begin_download(hwnd);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_cancel = true;
    return 0;
}
