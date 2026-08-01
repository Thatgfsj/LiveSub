#pragma once
// 设置窗口：Win32 原生控件，修改配置并立即生效。
// Tab 分页布局（字幕显示 / 识别 / 音频 / 输出），可缩放，子控件按比例跟随。
#include <functional>
#include <string>
#include <utility>
#include <vector>

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
    // 记录子控件初始客户区布局（缩放基准）
    void snapshot_layout();
    // 窗口尺寸变化时按比例重排所有子控件
    void resize_children(int w, int h);

    Config& cfg_;
    std::function<void()> on_apply_;
    std::function<void()> on_reload_devices_;
    HWND hwnd_ = nullptr;
    HFONT hfont_ = nullptr; // 仿宋字体
    bool done_ = false;

    // 缩放支持
    std::vector<std::pair<HWND, RECT>> layout_; // 子控件初始布局快照
    int base_w_ = 0, base_h_ = 0;               // 初始客户区尺寸
};
