// LiveSub 主入口（GUI 程序：双击运行，无控制台窗口）
// 用法:
//   livesub [--config <path>] [--settings] [--wav <file>] [--list-devices]
#include <cstdio>
#include <string>

#include <windows.h>
#include <shellapi.h>

#include "app.h"

static std::string wide_to_utf8(const wchar_t* w) {
    if (!w || !*w) return "";
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

// GUI 模式下的错误提示
static void fatal(const std::string& msg) {
    const std::wstring w = utf8_to_wide(msg);
    MessageBoxW(nullptr, w.c_str(), L"LiveSub", MB_OK | MB_ICONERROR);
}

// 未处理异常捕获：崩溃现场写入 livesub.log（否则崩溃完全无痕，无法排查）
// 注意：异常环境里尽量少做事（不 malloc 复杂结构），fopen/fprintf 直写
static LONG WINAPI crash_handler(EXCEPTION_POINTERS* ep) {
    FILE* f = fopen("crash.log", "ab");
    if (f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d] 崩溃 code=0x%08lX addr=%p\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                (unsigned long)ep->ExceptionRecord->ExceptionCode,
                ep->ExceptionRecord->ExceptionAddress);
        fclose(f);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

int main() {
    SetUnhandledExceptionFilter(crash_handler);

    // 解析命令行（宽字符）
    std::string config_path = "config.ini";
    std::string wav_test;
    bool open_settings = false;
    bool list_devices = false;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; i++) {
        const std::wstring a = argv[i];
        if (a == L"--config" && i + 1 < argc) {
            config_path = wide_to_utf8(argv[++i]);
        } else if (a == L"--settings") {
            open_settings = true;
        } else if (a == L"--wav" && i + 1 < argc) {
            wav_test = wide_to_utf8(argv[++i]);
        } else if (a == L"--list-devices") {
            list_devices = true;
        }
    }
    LocalFree(argv);

    // 设备列表（开发调试用）
    if (list_devices) {
        std::string out;
        auto devs = WasapiCapture::list_devices();
        for (const auto& [name, id] : devs) {
            out += name + "\n";
        }
        fatal("可用麦克风:\n" + (out.empty() ? "(无)" : out));
        return 0;
    }

    // 配置路径解析：默认找 exe 目录下的 config.ini
    if (config_path == "config.ini") {
        config_path = resolve_path("config.ini");
    }

    // 首次运行：生成默认配置并自动打开设置窗引导
    bool first_run = false;
    FILE* f = fopen(config_path.c_str(), "rb");
    if (!f) {
        Config c;
        c.save(config_path);
        first_run = true;
    } else {
        fclose(f);
    }

    // 模型检测：按当前配置的 model_size 检查对应文件，缺失/不完整 → 提示启动下载器
    {
        const Config c = Config::load(config_path);
        const std::string mp = resolve_path("model");
        struct ModelFile { std::string path; long long min_bytes; };
        std::vector<ModelFile> need;
        if (c.model_size == "small") {
            need = {{mp + "\\Qwen3-ASR-0.6B-Q8_0.gguf", 500000000LL},
                    {mp + "\\mmproj-Qwen3-ASR-0.6B-bf16.gguf", 100000000LL}};
        } else if (c.model_size == "sensevoice") {
            need = {{mp + "\\sensevoice\\model.int8.onnx", 200000000LL},
                    {mp + "\\sensevoice\\tokens.txt", 10000LL}};
        } else if (c.model_size == "fast") {
            need = {{mp + "\\fast\\encoder.int8.onnx", 60000000LL},
                    {mp + "\\fast\\decoder.int8.onnx",  4000000LL},
                    {mp + "\\fast\\joiner.int8.onnx",    900000LL},
                    {mp + "\\fast\\tokens.txt",           10000LL}};
        } else {
            need = {{mp + "\\Qwen3-ASR-1.7B-Q8_0.gguf", 1000000000LL},
                    {mp + "\\mmproj-Qwen3-ASR-1.7B-bf16.gguf", 100000000LL}};
        }
        bool has_model = true;
        for (const auto& mf : need) {
            FILE* f = fopen(mf.path.c_str(), "rb");
            if (!f) { has_model = false; break; }
            _fseeki64(f, 0, SEEK_END);
            const long long sz = _ftelli64(f);
            fclose(f);
            if (sz <= mf.min_bytes) { has_model = false; break; }
        }
        if (!has_model && wav_test.empty()) {
            const int ret = MessageBoxW(nullptr,
                L"未检测到模型文件（model 目录）。\n\n"
                L"均衡 SenseVoice：中文最准，约 230MB（推荐）\n"
                L"极速 流式zipformer：最快，约 190MB\n"
                L"小模型 Qwen3-0.6B：约 1.1GB\n"
                L"大模型 Qwen3-1.7B：约 2.7GB\n\n"
                L"是否现在启动模型下载器？",
                L"LiveSub", MB_YESNO | MB_ICONQUESTION);
            if (ret == IDYES) {
                ShellExecuteW(nullptr, L"open", L"model-dl.exe", nullptr, nullptr, SW_SHOWNORMAL);
            }
            return 0;
        }
    }

    App app;
    if (!app.init(config_path, wav_test.empty())) {
        fatal("LiveSub 启动失败\n\n请查看 livesub.log（程序目录下）了解详情。");
        return 1;
    }

    if (!wav_test.empty()) {
        app.run_wav_test(wav_test);
        app.shutdown();
        return 0;
    }

    // 首次运行或指定 --settings：打开设置窗
    if (first_run || open_settings) {
        app.open_settings();
    }

    app.run();
    app.shutdown();
    // 模型切换确认重启：进程已干净退出（模型文件/GPU 已释放）再拉起新实例，
    // 新实例读取已保存的 config.ini → 加载新模型
    if (app.restart_pending()) {
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        ShellExecuteW(nullptr, L"open", exe, nullptr, nullptr, SW_SHOWNORMAL);
    }
    return 0;
}
