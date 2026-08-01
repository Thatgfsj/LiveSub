#include "text_output.h"

#include <cstdio>
#include <cwchar>

#include <windows.h>
#include <shlobj.h>

#include "config.h"

// 用宽字符 API 打开文件（UTF-8 路径 → 中文文件名正确）
static FILE* open_utf8(const std::string& path, const wchar_t* mode) {
    const std::wstring w = utf8_to_wide(path);
    return _wfopen(w.c_str(), mode);
}

// 本地时间 HH:MM:SS
static std::string now_hh_mm_ss() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    return buf;
}

// 桌面目录（UTF-8）
static std::string desktop_dir() {
    wchar_t buf[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr,
                                SHGFP_TYPE_CURRENT, buf))) {
        return "";
    }
    return wide_to_utf8(buf);
}

// 记录文件名：讲话记录_YYYYMMDD_HHMMSS.txt
static std::string record_filename() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[64];
    snprintf(buf, sizeof(buf), "讲话记录_%04d%02d%02d_%02d%02d%02d.txt",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

// 输出文件时间戳（YYYYMMDD_HHMMSS）
static std::string now_stamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d%02d%02d_%02d%02d%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

void TextOutput::configure(const Config& c) {
    cfg_ = c;
    // 输出文件按时间命名（每次运行生成新文件，便于归档）；仅替换默认路径
    if (cfg_.write_text && cfg_.text_path == "subtitles.txt") {
        cfg_.text_path = "字幕_" + now_stamp() + ".txt";
    }
    if (cfg_.write_srt && cfg_.srt_path == "subtitles.srt") {
        cfg_.srt_path = "字幕_" + now_stamp() + ".srt";
    }
}

void TextOutput::open_files() {
    if (cfg_.write_text) {
        if (text_file_) { fclose(text_file_); text_file_ = nullptr; }
        text_file_ = open_utf8(cfg_.text_path, L"wb");
    }
    if (cfg_.write_srt) {
        if (srt_file_) { fclose(srt_file_); srt_file_ = nullptr; }
        srt_file_ = open_utf8(cfg_.srt_path, L"wb");
        seq_ = 0;
    }
}

void TextOutput::update(const std::string& partial, const std::string& finalized) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!text_file_ && !srt_file_) {
        open_files();
    }
    if (cfg_.write_text && text_file_) {
        // 文本文件总是写"当前完整字幕"（OBS 文本源轮询）
        fseek(text_file_, 0, SEEK_SET);
        fwrite(partial.data(), 1, partial.size(), text_file_);
        if (!finalized.empty()) {
            fwrite("\n", 1, 1, text_file_);
            fwrite(finalized.data(), 1, finalized.size(), text_file_);
        }
        fflush(text_file_);
    }
    if (cfg_.write_srt && srt_file_) {
        // 只追加定稿句
        if (!finalized.empty()) {
            std::string entry = std::to_string(seq_++) + "\n" +
                now_hh_mm_ss() + ",000 --> " + now_hh_mm_ss() + ",000\n" +
                finalized + "\n\n";
            fwrite(entry.data(), 1, entry.size(), srt_file_);
            fflush(srt_file_);
        }
    }
}

bool TextOutput::start_recording(std::string* file_out) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (recording_) return false;
    const std::string dir = desktop_dir();
    if (dir.empty()) return false;
    record_path_ = dir + "\\" + record_filename();
    record_file_ = open_utf8(record_path_, L"wb");
    if (!record_file_) return false;
    recording_ = true;
    const std::string head = "【讲话记录】" + now_hh_mm_ss() + " 开始\n";
    fwrite(head.data(), 1, head.size(), record_file_);
    fflush(record_file_);
    if (file_out) *file_out = record_path_;
    return true;
}

void TextOutput::stop_recording() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!recording_) return;
    const std::string tail = "【记录结束】" + now_hh_mm_ss() + "\n";
    fwrite(tail.data(), 1, tail.size(), record_file_);
    fflush(record_file_);
    fclose(record_file_);
    record_file_ = nullptr;
    recording_ = false;
}

void TextOutput::append_record(const std::string& text) {
    if (text.empty()) return;
    std::lock_guard<std::mutex> lk(mtx_);
    if (!recording_ || !record_file_) return;
    const std::string line = "[" + now_hh_mm_ss() + "] " + text + "\n";
    fwrite(line.data(), 1, line.size(), record_file_);
    fflush(record_file_);
}

void TextOutput::clear() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (text_file_) { fclose(text_file_); text_file_ = nullptr; }
    if (srt_file_)  { fclose(srt_file_);  srt_file_  = nullptr; }
    if (record_file_) { fclose(record_file_); record_file_ = nullptr; }
    recording_ = false;
}

TextOutput::~TextOutput() {
    clear();
}
