#pragma once
// 应用主类：双字幕管线（麦克风 / 电脑声音）共享单个 ASR 引擎（显存一份）
// 采集线程×2 → VAD×2 → 分窗队列×2 → 单 ASR 线程串行识别 → 单个共享字幕窗口
// （同一展示框整窗显示，两条字幕一般不同时开启，不做上下分割）
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <chrono>
#include <cstdarg>
#include <cstdio>

#include <windows.h>

#include "config.h"
#include "audio/wasapi_capture.h"
#include "audio/resampler.h"
#include "asr/vad.h"
#include "asr/audio_queue.h"
#include "asr/asr_engine.h"
#include "asr/text_merge.h"
#include "ui/subtitle_window.h"
#include "ui/settings_window.h"
#include "ui/tray_icon.h"
#include "output/text_output.h"
#include "input/voice_input.h"

// 一条字幕管线（采集→VAD→队列→合并→窗口）
struct AsrPipeline {
    std::string name;                      // 显示名（"麦克风"/"电脑声音"）
    std::atomic<bool> enabled{false};

    // 采集（采集线程）
    WasapiCapture capture;
    Resampler* resampler = nullptr;
    Vad* vad = nullptr;
    AudioQueue* queue = nullptr;
    std::vector<float> resample_buf;

    // 共享状态（采集线程写 / ASR 线程读）
    std::atomic<bool> speaking{false};
    std::atomic<bool> finalize_pending{false};
    std::atomic<size_t> seg_start{0};
    std::atomic<size_t> seg_end{0};
    // 定稿段边界快照：speech_end 时记录，避免新段 speech_start 覆盖 seg_start
    // 导致 finalize 取段失败（定稿/语音输入丢失）
    std::atomic<size_t> finalize_seg_start{0};
    std::atomic<size_t> finalize_seg_end{0};
    std::atomic<size_t> last_processed{0};
    // 新段开始清空上一句（避免"定稿句+新句"混存导致行数问题）；
    // 采集线程只置标志，ASR 线程处理（避免跨线程直接操作 merger）
    std::atomic<bool> clear_merger{false};

    // 识别与显示
    TextMerger merger;
    SubtitleWindow* window = nullptr;  // 指向 App 的唯一共享字幕窗口
    std::vector<float> win_buf;
    TextOutput output;
};

// 小工具
inline int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

class App {
public:
    App() = default;
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool init(const std::string& config_path, bool enable_capture = true);
    void run();       // Win32 消息循环（主线程）
    void shutdown();

    void open_settings();
    void apply_config();

    // 用 wav 模拟麦克风流（测试）
    bool run_wav_test(const std::string& wav_path);

    // 切换管线开关（托盘调用）
    void toggle_pipeline(AsrPipeline& p, bool enable);

private:
    // 采集回调（高频，运行在采集线程）
    void on_audio(AsrPipeline& p, const float* pcm, size_t n, int64_t t_ms);

    // ASR 线程主循环（共享单引擎，串行处理两条管线）
    void asr_loop();
    // 处理一条管线的当前窗口；返回是否识别了一窗
    bool process_pipeline(AsrPipeline& p);
    // 管线启停（start_capture=false 时只建识别链不采集，供 wav 测试）
    bool start_pipeline(AsrPipeline& p, bool is_mic, bool start_capture = true);
    void stop_pipeline(AsrPipeline& p);
    // 唯一共享字幕窗口（麦克风主轨上半区 / 电脑声音第二轨下半区），惰性创建
    bool ensure_window();
    // 模型文件检测：切换模型大小后对应文件缺失 → 提示并打开下载器；
    // 返回 true = 目标模型文件缺失（缺失时不再弹重启提示）
    bool check_model_files();

    Config cfg_;

    // 两条字幕管线
    AsrPipeline mic_;   // 麦克风字幕（默认开）
    AsrPipeline pc_;    // 电脑字幕（默认关）

    // 唯一共享字幕窗口（两条管线共用同一展示框，整窗显示当前识别的字幕）
    SubtitleWindow window_;

    // 共享单引擎（显存一份），ASR 线程串行使用
    AsrEngine asr_;
    std::mutex asr_mtx_;

    // 管线启停与 ASR 线程互斥：
    // stop_pipeline/start_pipeline 会 delete queue/vad/resampler，
    // 若 ASR 线程正 process_pipeline 使用它们 → use-after-free 崩溃。
    // 锁顺序：pipeline_mtx_ → asr_mtx_（无反向，不会死锁）
    std::mutex pipeline_mtx_;

    std::thread asr_thread_;
    std::atomic<bool> asr_running_{false};

    // 托盘
    TrayIcon tray_;
    bool tray_ready_ = false;
    void update_tray(TrayIcon::State s, const std::string& tip);

    // 语音输入（开启后定稿句注入当前焦点窗口）
    VoiceInput voice_input_;

    // 当前运行中的模型大小（init 时记录；设置切换后提示重启生效）
    std::string running_model_size_;

    // 日志
    FILE* log_file_ = nullptr;
    void logf(const char* fmt, ...);

    int64_t last_level_ms_ = 0;

    // ASR 心跳
    std::atomic<int64_t> last_asr_heartbeat_ms_{0};

    // 全屏检测（根本方案）：监听前台窗口变化事件，
    // 全屏应用（播放器/浏览器/无边框游戏）出现瞬间立即把字幕窗口提到置顶层顶部
    HWINEVENTHOOK win_event_hook_ = nullptr;
    static void CALLBACK on_foreground_event(HWINEVENTHOOK h, DWORD ev, HWND hwnd,
                                             LONG id, LONG cid, DWORD t, DWORD t2);

    std::atomic<bool> shutdown_done_{false};
};
