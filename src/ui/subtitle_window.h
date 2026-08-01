#pragma once
// 字幕显示窗口：分层透明窗口（WS_EX_LAYERED + UpdateLayeredWindow），
// D2D 渲染到内存 DIB 后上传，每像素 alpha 完全生效。
// 这是 OBS "窗口捕获" 的目标窗口
#include <string>
#include <atomic>
#include <mutex>

#include <windows.h>

struct ID2D1Factory;
struct ID2D1DCRenderTarget;
struct ID2D1SolidColorBrush;
struct IDWriteFactory;
struct IDWriteTextFormat;
struct IDWriteTextLayout;

class SubtitleWindow {
public:
    struct Style {
        std::wstring font_family = L"Microsoft YaHei UI";
        float font_size = 42.0f;
        DWORD font_color = 0xFFFFFFFF;   // AARRGGBB
        DWORD bg_color   = 0xC0000000;   // AARRGGBB 半透明黑
        int window_w = 1280;
        int window_h = 200;
        int window_x = -1;               // -1 = 屏幕底部居中
        int window_y = -1;
        int max_lines = 2;
        bool always_on_top = true;
        bool click_through = false;
        bool show_status = true;
        int fps = 30;
        int fade_in_ms  = 300;           // 字幕出现渐入时长
        int fade_out_ms = 500;           // 字幕消失渐出时长
        bool stroke_enabled = true;      // 文字描边（艺术字效果）
        DWORD stroke_color  = 0xFF000000; // 描边颜色 AARRGGBB（默认黑）
        int stroke_width    = 2;         // 描边粗细（像素）
    };

    bool create(const Style& s, std::wstring* err = nullptr);
    void destroy();

    // 线程安全：更新主轨字幕内容（UTF-8）；confirmed_offset 为已确认部分偏移
    // （其前实色显示，其后为未确认 interim 半透明显示）
    void set_text(const std::string& text, size_t confirmed_offset = std::string::npos);
    // 第二轨（电脑字幕）：更新下半区内容
    void set_second_text(const std::string& text, size_t confirmed_offset = std::string::npos);
    // 设置状态文本（如"识别中…"），UTF-8
    void set_status(const std::string& status);

    HWND hwnd() const { return hwnd_; }
    bool ok() const { return hwnd_ != nullptr; }

    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

private:
    void apply_style();
    void render();
    void release_d2d();
    std::wstring to_wide(const std::string& s) const;

    Style style_;
    HWND hwnd_ = nullptr;

    // D2D 资源（主线程创建/使用）
    ID2D1Factory* d2d_factory_ = nullptr;
    ID2D1DCRenderTarget* target_ = nullptr;
    ID2D1SolidColorBrush* bg_brush_ = nullptr;
    ID2D1SolidColorBrush* text_brush_ = nullptr;
    ID2D1SolidColorBrush* interim_brush_ = nullptr; // interim 半透明样式
    ID2D1SolidColorBrush* stroke_brush_ = nullptr;  // 描边色（默认黑）
    IDWriteFactory* dwrite_factory_ = nullptr;
    IDWriteTextFormat* text_format_ = nullptr;
    IDWriteTextLayout* layout_ = nullptr;  // 主轨布局缓存
    IDWriteTextLayout* layout2_ = nullptr; // 第二轨布局缓存

    // 文本变化时重建布局（跨线程标记）
    std::atomic<bool> layout_dirty_{true};
    float last_layout_size_ = 0.0f; // 上次布局字号（平滑用，避免缩放跳变）
    size_t confirmed_offset_ = std::string::npos; // 主轨已确认偏移（interim 样式）
    size_t second_confirmed_ = std::string::npos; // 第二轨已确认偏移
    std::wstring second_text_;                    // 第二轨文本（互斥锁保护）
    void rebuild_layout(const std::wstring& text, float w, float h);
    void rebuild_layout2(const std::wstring& text, float w, float h);
    void apply_interim_style(IDWriteTextLayout* l, const std::wstring& t);
    void draw_layout(IDWriteTextLayout* l, float x, float y, bool stroke);

    // 分层窗口资源
    HDC mem_dc_ = nullptr;
    HBITMAP dib_ = nullptr;
    void* dib_bits_ = nullptr;
    int dib_w_ = 0, dib_h_ = 0;

    // 内容（跨线程：互斥锁保护，避免 set/渲染并发 use-after-free）
    mutable std::mutex mtx_;
    std::wstring content_;
    std::wstring status_;

    // 淡入淡出动画（主线程 render 推进）
    float alpha_ = 0.0f;          // 当前窗口 alpha 0-1
    int64_t last_anim_ms_ = 0;    // 上次动画推进时间
    bool  anim_has_content_ = false; // 上次内容是否有文本

    UINT_PTR timer_ = 0;
};
