#pragma once
// 应用主类：编排 采集线程 → VAD → 分窗队列 → ASR 线程 → 字幕窗口
#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include <memory>
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

    // 初始化（加载配置、创建窗口、启动线程）；失败返回 false
    bool init(const std::string& config_path, bool enable_capture = true);
    void run();       // Win32 消息循环（主线程）
    void shutdown();

    // 用 wav 文件模拟麦克风流（实时节奏回放，验证流式链路），完成后自动退出
    bool run_wav_test(const std::string& wav_path);

    // 打开设置窗（模态）
    void open_settings();

    // 重新加载配置并应用（设置窗"应用"时调用）
    void apply_config();

private:
    // 采集线程回调（高频）
    void on_audio(const float* pcm, size_t n, int64_t t_ms);
    // ASR 线程主循环
    void asr_loop();

    Config cfg_;

    // 采集（主线程创建，回调在采集线程）
    WasapiCapture capture_;
    Resampler* resampler_ = nullptr;      // 48k→16k
    Vad* vad_ = nullptr;
    AudioQueue* queue_ = nullptr;

    // 识别（ASR 线程）
    AsrEngine asr_;
    TextMerger merger_;
    std::thread asr_thread_;
    std::atomic<bool> asr_running_{false};

    // 字幕（主线程）
    SubtitleWindow window_;
    TextOutput output_;
    TrayIcon tray_;
    bool tray_ready_ = false;

    // 托盘状态同步
    void update_tray(TrayIcon::State s, const std::string& tip);

    // 共享状态
    std::atomic<bool> speaking_{false};       // VAD 语音中
    std::atomic<bool> finalize_pending_{false}; // 静音到达，请求定稿
    std::vector<float> resample_buf_;
    std::vector<float> win_buf_;

    // 状态栏文本
    std::string status_text_;

    // 日志文件（livesub.log）
    FILE* log_file_ = nullptr;
    void logf(const char* fmt, ...);

    // 电平显示节流
    int64_t last_level_ms_ = 0;
    float last_level_db_ = -100.0f;

    // ASR 线程心跳（检测识别引擎挂起）
    std::atomic<int64_t> last_asr_heartbeat_ms_{0};
    std::atomic<int64_t> last_finalize_ms_{0};   // 上次定稿时间
    std::atomic<size_t> seg_start_total_{0};     // 当前语音段起点（VAD 分段驱动识别）
    std::atomic<size_t> seg_end_total_{0};       // 语音段结束点（on_speech_end 记录，定稿窗口终点）
    std::atomic<size_t> last_processed_total_{0}; // 上次已识别到的音频位置（超时醒来跳过重复）
    size_t last_seg_start_ = 0;                  // 上次处理的语音段起点（ASR 线程私有）

    // 关闭幂等保护
    std::atomic<bool> shutdown_done_{false};
};
