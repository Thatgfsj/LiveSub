#pragma once
// 配置加载/保存（INI 格式，UTF-8 无 BOM，支持 # 注释）
#include <string>
#include <map>
#include <optional>

struct Config {
    // [audio]
    int  sample_rate        = 48000;   // 采集设备采样率（WASAPI 共享模式）
    std::string device_id   = "";      // 空 = 默认输入设备
    float input_boost_db    = 0.0f;    // 输入增益补偿

    // [vad]
    float vad_threshold_db  = -52.0f;  // 手动语音门限（dBFS）；0 = 自适应（底噪+余量）
    float vad_margin_db     = 12.0f;   // 自适应模式余量
    int   min_speech_ms     = 250;     // 最短语音持续（防误触发）
    int   silence_ms        = 800;     // 句末静音提交时间

    // [asr]
    std::string model_size  = "large";  // 模型大小：large=1.7B（更准/要求高） small=0.6B（更快/要求低）
    std::string model_path  = "model/Qwen3-ASR-1.7B-Q8_0.gguf";
    std::string mmproj_path = "model/mmproj-Qwen3-ASR-1.7B-bf16.gguf";
    int  n_threads          = 18;      // 推理线程数
    int  gpu_layers         = 999;     // 主模型 GPU 层数（Vulkan；999=全部）
    int  n_batch            = 256;
    int  chunk_ms           = 6000;    // 识别窗口长度（含上下文，语音中段识别更稳）
    int  hop_ms             = 800;     // 窗口滑动步长（更新频率与稳定性平衡）
    int  max_new_tokens     = 128;
    std::string prompt      = "Transcribe the audio.";

    // [ui]
    std::string font_family = "Microsoft YaHei UI";
    float font_size         = 42.0f;
    std::string font_color  = "#FFFFFF";
    std::string bg_color    = "#33000000"; // AARRGGBB 半透明黑（透明度 20%）
    bool  stroke_enabled    = true;     // 文字描边（艺术字效果）
    std::string stroke_color = "#000000"; // 描边颜色（默认黑色）
    int   stroke_width     = 2;        // 描边粗细（像素）
    int   window_w          = 1280;
    int   window_h          = 480;
    int   pos_x             = 960;     // 字幕窗口中心点 X（像素，默认 960 居中）
    int   pos_y             = 1250;    // 字幕窗口中心点 Y（像素，默认 1250）
    int   max_lines         = 2;       // 固定 2 行：上一句 + 当前句
    bool  always_on_top     = true;
    bool  click_through     = true;    // 鼠标穿透（默认开启，不挡直播操作）
    bool  show_status       = true;    // 显示"聆听中…"状态
    int   fade_in_ms        = 300;     // 字幕出现渐入（毫秒）
    int   fade_out_ms       = 500;     // 字幕消失渐出（毫秒）
    int   fps               = 30;

    // [output]
    bool  write_srt         = false;    // 写 SRT 字幕（与 write_text 二选一，默认关闭）
    std::string srt_path    = "subtitles.srt";
    bool  write_text        = false;    // 写文本文件（默认关闭；讲话稿记录走托盘"开始记录"）
    std::string text_path   = "subtitles.txt";

    // [tracks]
    bool mic_enabled        = true;    // 麦克风字幕（默认开）
    int  mic_pos_x          = 50;      // 麦克风字幕位置（屏幕百分比）
    int  mic_pos_y          = 85;
    bool pc_enabled         = false;   // 电脑字幕（默认关：识别电脑播放的声音）
    int  pc_pos_x           = 50;
    int  pc_pos_y           = 45;

    // [log]
    int  log_level          = 1;       // 0=quiet 1=info 2=debug

    static Config load(const std::string& path);
    void save(const std::string& path) const;
    std::string path() const { return path_; }
    void set_path(const std::string& p) { path_ = p; }

private:
    std::string path_;
};

// 简单工具
std::string trim(const std::string& s);
// 解析 #RRGGBB 或 #AARRGGBB → 0xAARRGGBB
std::optional<unsigned> parse_color(const std::string& s);
// 16 进制字符
int hex_val(char c);

// 发布版路径工具：以 exe 所在目录为基准解析相对路径
// （exe 在任意工作目录下启动都能找到模型/配置）
std::string exe_dir();
std::string resolve_path(const std::string& p);

// UTF-8 <-> UTF-16 转换（Windows 文件名/界面文本）
std::wstring utf8_to_wide(const std::string& s);
std::string  wide_to_utf8(const std::wstring& s);
