// model-dl.exe：模型下载器（安装器调用 / 主程序首启兜底）
// 用法: model-dl.exe [model_dir]
// 启动时选择模型大小：
//   - 大模型（1.7B）：更准确，要求更高性能，约 2.8GB（推荐）
//   - 小模型（0.6B）：速度快、要求低，约 1.1GB
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

static const char* kBaseMir = "https://hf-mirror.com/ggml-org/";
static const char* kBaseHf  = "https://huggingface.co/ggml-org/";
static const char* kRepo    = "Qwen3-ASR-1.7B-GGUF/resolve/main/";
static const char* kRepoSm  = "Qwen3-ASR-0.6B-GGUF/resolve/main/";

struct ModelChoice {
    const char* main_name;
    const char* proj_name;
    uint64_t main_size;
    uint64_t proj_size;
};

static const ModelChoice kLarge = {
    "Qwen3-ASR-1.7B-Q8_0.gguf", "mmproj-Qwen3-ASR-1.7B-bf16.gguf",
    2165034944ull, 641773984ull,
};
static const ModelChoice kSmall = {
    "Qwen3-ASR-0.6B-Q8_0.gguf", "mmproj-Qwen3-ASR-0.6B-bf16.gguf",
    804749248ull, 378575520ull,
};

static HWND g_progress = nullptr, g_status = nullptr, g_done = nullptr;
static std::atomic<bool> g_cancel{false};
static std::atomic<bool> g_use_large{true};

static void set_status(const std::wstring& s) {
    if (g_status) SetWindowTextW(g_status, s.c_str());
}

static void download_worker(const std::string& model_dir, const ModelChoice& mc,
                            const std::string& repo) {
    const std::string main_path = model_dir + "\\" + mc.main_name;
    const std::string proj_path = model_dir + "\\" + mc.proj_name;

    const DownloadFile files[] = {
        { (std::string(kBaseMir) + repo + mc.main_name).c_str(),
          (std::string(kBaseHf) + repo + mc.main_name).c_str(), main_path, mc.main_size },
        { (std::string(kBaseMir) + repo + mc.proj_name).c_str(),
          (std::string(kBaseHf) + repo + mc.proj_name).c_str(), proj_path, mc.proj_size },
    };
    const wchar_t* names[] = { L"主模型", L"音频编码器" };

    bool all_ok = true;
    std::string last_err;
    for (int i = 0; i < 2; i++) {
        set_status(std::wstring(L"正在下载 ") + names[i] + L" ...");
        int rc = 0;
        for (int retry = 0; retry < 3 && !g_cancel; retry++) {
            std::string dl_err;
            rc = download_file(files[i],
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
                        // 服务端未给总长度：显示已下载量
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
        if (rc != 0) { all_ok = false; break; }
        SendMessageW(g_progress, PBM_SETPOS, 100, 0);
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
    g_use_large = (SendMessageW(GetDlgItem(hwnd, 2), BM_GETCHECK, 0, 0) == BST_CHECKED);
    const ModelChoice& mc = g_use_large ? kLarge : kSmall;
    const std::string repo = g_use_large ? kRepo : kRepoSm;
    std::thread(download_worker, get_model_dir(), mc, repo).detach();
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR lpCmdLine, int) {
    // 命令行：--auto [large|small] 直接开始下载（跳过选择界面）
    bool auto_start = false;
    {
        std::wstring cmd = lpCmdLine ? lpCmdLine : L"";
        if (cmd.find(L"--auto") != std::wstring::npos) auto_start = true;
        if (cmd.find(L"--small") != std::wstring::npos) g_use_large = false;
        if (cmd.find(L"--large") != std::wstring::npos) g_use_large = true;
    }
    const std::string model_dir = get_model_dir();
    CreateDirectoryA(model_dir.c_str(), nullptr);

    // 窗口：选择模型大小 + 进度
    const wchar_t* cls = L"LiveSubModelDl";
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.hInstance = hInst;
    wc.lpfnWndProc = [](HWND h, UINT m, WPARAM wp, LPARAM lp) -> LRESULT {
        if (m == WM_COMMAND) {
            const int id = LOWORD(wp);
            if (id == 1) { // 开始下载
                begin_download(h);
                return 0;
            }
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
        CW_USEDEFAULT, CW_USEDEFAULT, 520, 220, nullptr, nullptr, hInst, nullptr);
    CreateWindowExW(0, L"STATIC", L"选择要下载的模型大小：",
        WS_CHILD | WS_VISIBLE, 20, 12, 460, 20, hwnd, nullptr, hInst, nullptr);
    HWND r_big = CreateWindowExW(0, L"BUTTON", L"大模型（1.7B）：更准确，要求更高性能，约 2.8GB（推荐）",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | BS_LEFT,
        20, 36, 470, 22, hwnd, (HMENU)2, hInst, nullptr);
    CreateWindowExW(0, L"BUTTON", L"小模型（0.6B）：速度快、性能要求低，约 1.1GB",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | BS_LEFT,
        20, 60, 470, 22, hwnd, (HMENU)3, hInst, nullptr);
    SendMessageW(r_big, BM_SETCHECK, BST_CHECKED, 0);

    g_progress = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE,
                                 20, 92, 470, 24, hwnd, nullptr, hInst, nullptr);
    SendMessageW(g_progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    g_status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                               20, 122, 470, 20, hwnd, nullptr, hInst, nullptr);
    CreateWindowExW(0, L"BUTTON", L"开始下载", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    320, 152, 90, 26, hwnd, (HMENU)1, hInst, nullptr);
    g_done = CreateWindowExW(0, L"BUTTON", L"关闭", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                             420, 152, 70, 26, hwnd, (HMENU)4, hInst, nullptr);
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
