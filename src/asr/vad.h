#pragma once
// 自适应 VAD：底噪跟踪（最小值统计） + 阈值 = 底噪 + 余量
// - 分帧计算 RMS（dBFS）
// - 底噪估计：帧能量最小值跟踪（快速下降、慢速回升），自动适应环境噪音
// - 语音判定：帧能量 > noise_floor + margin
// - 句末定稿：语音后静音持续 silence_ms
#include <cstddef>
#include <cstdint>
#include <vector>
#include <functional>

class Vad {
public:
    struct Params {
        int   sample_rate  = 16000;
        float threshold_db = -52.0f; // 手动门限（dBFS）；0 = 自适应
        float margin_db    = 12.0f;  // 自适应模式：门限 = 底噪 + margin（dB）
        int   min_speech_ms = 250;  // 最短语音（毫秒）
        int   silence_ms   = 800;   // 判定"句结束"的静音时长（毫秒）
        int   frame_ms     = 20;    // 帧长
        float max_rms      = 0.999f; // 防爆音上限
    };

    explicit Vad(const Params& p);

    // 喂入一块音频；t_ms 为块尾时间（毫秒，单调递增）
    void process(const float* pcm, size_t n, int64_t t_ms);

    // 每帧电平回调（dBFS，已平滑），用于界面显示实时音量
    std::function<void(float db)> on_level;

    // 状态回调（语音段起止，毫秒时间线）
    std::function<void(int64_t t_ms)> on_speech_start;
    std::function<void(int64_t t_ms)> on_speech_end;

    // 强制结束当前语音段（如程序退出/清空）
    void force_end(int64_t t_ms);

    bool in_speech() const { return in_speech_; }

    // 当前自适应阈值与底噪（dB），供界面显示与窗口级判断
    float current_threshold_db() const {
        return (p_.threshold_db != 0.0f) ? p_.threshold_db
                                         : noise_floor_db_ + p_.margin_db;
    }
    float noise_floor_db() const { return noise_floor_db_; }

    void set_params(const Params& p) { p_ = p; reset(); }
    void reset();

private:
    Params p_;
    int frame_len_;
    std::vector<float> buf_;
    bool  in_speech_ = false;
    int64_t speech_candidate_ms_ = -1; // 候选语音起点（满足 min_speech 后确认）
    int64_t speech_start_ms_ = 0;
    int64_t last_voice_ms_   = 0;
    int64_t last_processed_ms_ = 0;

    // 底噪自适应（最小值跟踪）
    float noise_floor_db_ = -60.0f;
    float smooth_db_ = -60.0f; // 平滑电平（界面显示用）

    void feed_frame(const float* pcm, int64_t t_ms);
};
