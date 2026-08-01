#pragma once
// 字幕文件输出：纯文本（供 OBS 文本源轮询）与 SRT
// 讲话稿记录：实时追加到桌面文件（文件名按时间命名），供直播后整理
#include <string>
#include <mutex>
#include <cstdio>

class TextOutput {
public:
    struct Config {
        bool write_text = false;   // 写文本文件（默认关闭）
        std::string text_path = "subtitles.txt";
        bool write_srt = false;
        std::string srt_path = "subtitles.srt";
    };

    TextOutput() = default;
    ~TextOutput();
    TextOutput(const TextOutput&) = delete;
    TextOutput& operator=(const TextOutput&) = delete;

    void configure(const Config& c) { cfg_ = c; }

    // 更新当前字幕（partial: 部分结果；final: 定稿句）
    void update(const std::string& partial, const std::string& finalized);

    // ---- 讲话稿记录 ----
    // 开始记录：在桌面创建"讲话记录_YYYYMMDD_HHMMSS.txt"
    bool start_recording(std::string* file_out = nullptr);
    void stop_recording();
    // 追加一句定稿文本（带时间戳）
    void append_record(const std::string& text);
    bool recording() const { return recording_; }

    // 清空文件
    void clear();

private:
    Config cfg_;
    mutable std::mutex mtx_;
    FILE* text_file_ = nullptr;
    FILE* srt_file_ = nullptr;
    FILE* record_file_ = nullptr;
    std::string record_path_;
    bool recording_ = false;
    int seq_ = 0;

    void open_files();
};
