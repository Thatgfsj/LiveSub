#pragma once
// 设置窗口：Win32 原生控件，修改配置并立即生效。
// Tab 分页布局（字幕显示 / 识别 / 音频 / 输出），可缩放；
// 缩放时内容布局固定不变（Tab 拉宽、底部按钮贴底），避免控件错乱。
#include <functional>
#include <string>

#include <windows.h>

struct Config;

class SettingsWindow {
public:
    // 打开设置窗（模态，阻塞直到关闭）
    SettingsWindow(Config& cfg,
                   std::function<void()> on_apply,
                   std::function<void()> on_reload_devices);
    ~SettingsWindow();
    void run(); // 消息循环直到关闭

private:
    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void populate_devices();
    void read_fields();
    void fill_fields();
    void apply();
    // 切换 Tab 页（只显示当前页控件）
    void show_page(int page);
    // 窗口尺寸变化：Tab 拉宽、底部按钮贴底（内容布局不变）
    void on_size(int w, int h);

    Config& cfg_;
    std::function<void()> on_apply_;
    std::function<void()> on_reload_devices_;
    HWND hwnd_ = nullptr;
    HFONT hfont_ = nullptr; // 仿宋字体
    bool done_ = false;

    int min_w_ = 0, min_h_ = 0; // 初始窗口总尺寸（缩放最小限制）
};
