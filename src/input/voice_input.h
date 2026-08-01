#pragma once
// 语音输入：开启后，定稿的识别文本直接输入到当前焦点窗口
// 实现：SendInput + KEYEVENTF_UNICODE 逐字符注入（不经过键盘布局、不动剪贴板）
#include <atomic>
#include <string>

class VoiceInput {
public:
    bool enabled() const { return enabled_.load(); }
    void set_enabled(bool e) { enabled_.store(e); }

    // 把一段文本输入到当前前台窗口（UTF-8 输入，支持中文/英文/标点）
    // 在调用线程执行（ASR 线程），SendInput 是线程安全的
    void commit_text(const std::string& utf8);

private:
    std::atomic<bool> enabled_{false};
};
