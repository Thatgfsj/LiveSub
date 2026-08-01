#pragma once
// 系统托盘图标：右下角常驻状态指示 + 右键菜单
// 状态（图标颜色）：加载中(灰) / 就绪(蓝) / 识别中(绿) / 错误(红)
#include <functional>
#include <string>

#include <windows.h>
#include <shellapi.h>

class TrayIcon {
public:
    enum class State { Loading, Ready, Listening, Error };

    // 回调
    std::function<void()> on_open_settings;
    std::function<void()> on_toggle_window;   // 显示/隐藏字幕窗
    std::function<void()> on_toggle_record;   // 开始/结束记录讲话稿
    std::function<void()> on_toggle_voice;    // 开启/关闭语音输入
    std::function<void()> on_quit;

    // 记录状态（菜单项显示"开始/结束记录"）
    void set_recording(bool r) { recording_ = r; }
    bool recording() const { return recording_; }

    // 语音输入状态（菜单项显示"开启/关闭语音输入"）
    void set_voice_input(bool v) { voice_input_ = v; }
    bool voice_input() const { return voice_input_; }

    bool create(const std::wstring& tip);
    void set_state(State s, const std::wstring& tip = L"");
    void destroy();
    ~TrayIcon() { destroy(); }

private:
    static LRESULT CALLBACK host_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void show_menu();
    static HICON make_icon(COLORREF color);

    HWND host_ = nullptr;
    NOTIFYICONDATAW nid_ = {};
    HICON icons_[4] = {};
    bool added_ = false;
    bool recording_ = false;
    bool voice_input_ = false;
    State state_ = State::Loading;
};
