// model-dl.exe：模型下载器（安装器调用 / 主程序首启兜底）
// 用法: model-dl.exe [model_dir]
// 下载 Qwen3-ASR-1.7B 模型（Q8_0 主模型 + BF16 音频编码器）到 model_dir
#include <cstdio>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>

#include <windows.h>
#include <commctrl.h>

#include "tools/model_downloader.h"

static const char* kMainUrl  = "https://hf-mirror.com/ggml-org/Qwen3-ASR-1.7B-GGUF/resolve/main/Qwen3-ASR-1.7B-Q8_0.gguf";
static const char* kMainMir = "https://huggingface.co/ggml-org/Qwen3-ASR-1.7B-GGUF/resolve/main/Qwen3-ASR-1.7B-Q8_0.gguf";
static const char* kProjUrl = "https://hf-mirror.com/ggml-org/Qwen3-ASR-1.7B-GGUF/resolve/main/mmproj-Qwen3-ASR-1.7B-bf16.gguf";
static const char* kProjMir = "https://huggingface.co/ggml-org/Qwen3-ASR-1.7B-GGUF/resolve/main/mmproj-Qwen3-ASR-1.7B-bf16.gguf";

static const uint64_t kMainSize = 2165034944ull; // Q8_0 主模型字节数
static const uint64_t kProjSize = 641773984ull;  // mmproj BF16 字节数

static HWND g_progress = nullptr, g_status = nullptr, g_done = nullptr;
static std::atomic<bool> g_cancel{false};

static void set_status(const std::wstring& s) {
    if (g_status) SetWindowTextW(g_status, s.c_str());
}

// 下载线程
static void download_worker(const std::string& model_dir) {
    const std::string main_path = model_dir + "\\Qwen3-ASR-1.7B-Q8_0.gguf";
    const std::string proj_path = model_dir + "\\mmproj-Qwen3-ASR-1.7B-bf16.gguf";

    const DownloadFile files[] = {
        { kMainUrl, kMainMir, main_path, kMainSize },
        { kProjUrl, kProjMir, proj_path, kProjSize },
    };
    const wchar_t* names[] = { L"主模型 Q8_0 (2.0GB)", L"音频编码器 BF16 (0.6GB)" };

    bool all_ok = true;
    for (int i = 0; i < 2; i++) {
        set_status(std::wstring(L"正在下载 ") + names[i] + L" ...");
        int retry = 0;
        int rc = 0;
        do {
            rc = download_file(files[i],
                [i](uint64_t done, uint64_t total) -> bool {
                    if (g_cancel) return false;
                    if (total > 0) {
                        SendMessageW(g_progress, PBM_SETPOS, (WPARAM)(done * 100 / total), 0);
                        wchar_t buf[128];
                        swprintf(buf, 128, L"%.0f / %.0f MB", done / 1048576.0, total / 1048576.0);
                        SetWindowTextW(g_status, buf);
                    }
                    return true;
                }, nullptr);
            if (rc == 0) break;
            if (rc == 1 && retry < 5) { // 未完成 → 续传重试
                retry++;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            } else {
                break;
            }
        } while (retry < 5 && !g_cancel);
        if (rc != 0) { all_ok = false; break; }
        SendMessageW(g_progress, PBM_SETPOS, 100, 0);
    }

    set_status(all_ok ? L"模型下载完成！" : L"下载失败，请检查网络后重新运行安装程序");
    EnableWindow(g_done, TRUE);
    if (all_ok) SetFocus(g_done);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    // 参数：模型目录（默认 exe 所在目录的 model\）
    std::string model_dir;
    {
        wchar_t buf[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        std::wstring w(buf);
        size_t pos = w.find_last_of(L"\\/");
        if (pos != std::wstring::npos) w.resize(pos);
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
        model_dir.assign((size_t)n - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, model_dir.data(), n, nullptr, nullptr);
        model_dir += "\\model";
    }
    CreateDirectoryA(model_dir.c_str(), nullptr);

    // 简单窗口：进度条 + 状态 + 完成按钮
    const wchar_t* cls = L"LiveSubModelDl";
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.hInstance = hInst;
    wc.lpfnWndProc = [](HWND h, UINT m, WPARAM wp, LPARAM lp) -> LRESULT {
        if (m == WM_COMMAND && LOWORD(wp) == 1) { // 完成/取消
            if (IsWindowEnabled(GetDlgItem(h, 1))) {
                PostQuitMessage(0);
            } else {
                g_cancel = true;
                SetWindowTextW(h, L"正在取消...");
            }
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
        CW_USEDEFAULT, CW_USEDEFAULT, 460, 150, nullptr, nullptr, hInst, nullptr);
    g_progress = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE,
                                 20, 30, 420, 24, hwnd, nullptr, hInst, nullptr);
    SendMessageW(g_progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    g_status = CreateWindowExW(0, L"STATIC", L"准备下载...", WS_CHILD | WS_VISIBLE,
                               20, 60, 420, 20, hwnd, nullptr, hInst, nullptr);
    g_done = CreateWindowExW(0, L"BUTTON", L"完成", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED,
                             340, 90, 100, 26, hwnd, (HMENU)1, hInst, nullptr);
    ShowWindow(hwnd, SW_SHOW);

    std::thread worker(download_worker, model_dir);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_cancel = true;
    worker.join();
    return 0;
}
