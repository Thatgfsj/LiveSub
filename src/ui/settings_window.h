#pragma once
// 设置窗口：Win32 原生控件，修改配置并立即生效
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

    Config& cfg_;
    std::function<void()> on_apply_;
    std::function<void()> on_reload_devices_;
    HWND hwnd_ = nullptr;
    HFONT hfont_ = nullptr; // 仿宋字体
    bool done_ = false;
};
