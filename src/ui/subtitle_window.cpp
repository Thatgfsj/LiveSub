#include "subtitle_window.h"

#include <d2d1.h>
#include <dwrite.h>

#include <algorithm>
#include <cmath>
#include <vector>

// 分层窗口 + UpdateLayeredWindow：
//   D2D 渲染到内存 DIB（32bpp 预乘 alpha）→ 上传为窗口内容。
//   每像素 alpha 完全生效，背景可半透明/全透明，OBS WGC 捕获可保留透明度。

LRESULT CALLBACK SubtitleWindow::wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    SubtitleWindow* self = (SubtitleWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
            self = (SubtitleWindow*)cs->lpCreateParams;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
            self->hwnd_ = hwnd;
            return 0;
        }
        case WM_PAINT: {
            if (self) self->render();
            return 0;
        }
        case WM_TIMER:
            if (self) self->render();
            return 0;
        case WM_DESTROY:
            if (self && self->timer_) {
                KillTimer(hwnd, self->timer_);
                self->timer_ = 0;
            }
            return 0;
        case WM_NCHITTEST:
            if (self && self->style_.click_through) {
                return HTTRANSPARENT;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

bool SubtitleWindow::create(const Style& s, std::wstring* err) {
    destroy();
    style_ = s;

    const wchar_t* cls = L"LiveSubSubtitleWindow";
    HINSTANCE hinst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = cls;
    RegisterClassExW(&wc);

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_factory_))) {
        if (err) *err = L"Direct2D 初始化失败";
        return false;
    }
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   (IUnknown**)&dwrite_factory_))) {
        if (err) *err = L"DirectWrite 初始化失败";
        return false;
    }

    DWORD ex_style = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    if (style_.always_on_top) ex_style |= WS_EX_TOPMOST;
    if (style_.click_through) ex_style |= WS_EX_TRANSPARENT;

    RECT rc = {0, 0, style_.window_w, style_.window_h};
    AdjustWindowRectEx(&rc, WS_POPUP, FALSE, ex_style);

    hwnd_ = CreateWindowExW(ex_style, cls, L"LiveSub 字幕", WS_POPUP,
                            style_.window_x, style_.window_y,
                            rc.right - rc.left, rc.bottom - rc.top,
                            nullptr, nullptr, hinst, this);
    if (!hwnd_) {
        if (err) *err = L"创建字幕窗口失败";
        return false;
    }

    // 内存 DIB（32bpp top-down 预乘 alpha）
    const int w = style_.window_w, h = style_.window_h;
    HDC screen_dc = GetDC(nullptr);
    mem_dc_ = CreateCompatibleDC(screen_dc);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    dib_ = CreateDIBSection(screen_dc, &bi, DIB_RGB_COLORS, &dib_bits_, nullptr, 0);
    ReleaseDC(nullptr, screen_dc);
    if (!dib_ || !mem_dc_) {
        if (err) *err = L"创建渲染表面失败";
        return false;
    }
    SelectObject(mem_dc_, dib_);
    dib_w_ = w;
    dib_h_ = h;

    apply_style();
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);

    // 创建/重建后直接全不透明显示：
    // 不从头淡入——半透明期间文字边缘发虚（"加载时模糊一下"），
    // 只有内容从有到无时才淡出
    alpha_ = 1.0f;
    anim_has_content_ = true;

    // 惰性定时器：内容静止时完全无定时器（主线程零唤醒），
    // set_text/set_status 变化时才 SetTimer 唤醒一帧重绘，画完静止再关
    timer_ = 0;
    return true;
}

// 内容变化/动画期间需要定时重绘 → 确保定时器在跑（可从 ASR 线程调用）
void SubtitleWindow::ensure_timer() {
    if (!timer_ && hwnd_) {
        const UINT interval = std::max(1, 1000 / std::max(1, style_.fps));
        timer_ = SetTimer(hwnd_, 1, interval, nullptr);
    }
}

void SubtitleWindow::release_d2d() {
    if (layout_)         { layout_->Release();         layout_         = nullptr; }
    if (text_format_)    { text_format_->Release();    text_format_    = nullptr; }
    if (text_brush_)     { text_brush_->Release();     text_brush_     = nullptr; }
    if (interim_brush_)  { interim_brush_->Release();  interim_brush_  = nullptr; }
    if (stroke_brush_)   { stroke_brush_->Release();   stroke_brush_   = nullptr; }
    if (bg_brush_)       { bg_brush_->Release();       bg_brush_       = nullptr; }
    if (target_)         { target_->Release();         target_         = nullptr; }
}

void SubtitleWindow::apply_style() {
    if (!hwnd_) return;

    if (style_.always_on_top) {
        SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    LONG_PTR ex = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    if (style_.click_through) {
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex | WS_EX_TRANSPARENT);
    } else {
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex & ~WS_EX_TRANSPARENT);
    }

    release_d2d();
    if (d2d_factory_) {
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            0, 0, D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE);
        d2d_factory_->CreateDCRenderTarget(&props, &target_);
        if (target_) {
            target_->CreateSolidColorBrush(
                D2D1::ColorF((style_.bg_color >> 16 & 0xFF) / 255.0f,
                             (style_.bg_color >> 8 & 0xFF) / 255.0f,
                             (style_.bg_color & 0xFF) / 255.0f,
                             (style_.bg_color >> 24 & 0xFF) / 255.0f),
                &bg_brush_);
            target_->CreateSolidColorBrush(
                D2D1::ColorF((style_.font_color >> 16 & 0xFF) / 255.0f,
                             (style_.font_color >> 8 & 0xFF) / 255.0f,
                             (style_.font_color & 0xFF) / 255.0f,
                             (style_.font_color >> 24 & 0xFF) / 255.0f),
                &text_brush_);
            // interim（未确认尾部）：与已确认文本同色（白色实色）
            // 灰色/半透明在深色背景上对比度低，看起来模糊；
            // 用户不需要"未确认"的视觉区分，统一白色最清晰
            target_->CreateSolidColorBrush(
                D2D1::ColorF((style_.font_color >> 16 & 0xFF) / 255.0f,
                             (style_.font_color >> 8 & 0xFF) / 255.0f,
                             (style_.font_color & 0xFF) / 255.0f,
                             (style_.font_color >> 24 & 0xFF) / 255.0f),
                &interim_brush_);
            // 描边色（艺术字效果，默认黑）
            target_->CreateSolidColorBrush(
                D2D1::ColorF((style_.stroke_color >> 16 & 0xFF) / 255.0f,
                             (style_.stroke_color >> 8 & 0xFF) / 255.0f,
                             (style_.stroke_color & 0xFF) / 255.0f,
                             (style_.stroke_color >> 24 & 0xFF) / 255.0f),
                &stroke_brush_);
        }
    }
    if (dwrite_factory_) {
        dwrite_factory_->CreateTextFormat(style_.font_family.c_str(), nullptr,
            DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            style_.font_size, L"zh-CN", &text_format_);
        if (text_format_) {
            text_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            // 垂直居中：字幕在窗口内居中显示（默认 2 句场景窗口高度充足）
            text_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
    layout_dirty_ = true;
}

void SubtitleWindow::render() {
    if (!hwnd_ || !mem_dc_ || !dib_) return;
    if (IsWindowVisible(hwnd_) == FALSE) return;

    // 0. 静止跳过：无内容变化、无动画进行时零开销（不重绘、不 ULW）。
    //    长时间运行卡顿/黑屏的根因：WM_TIMER 每帧全量重绘 + UpdateLayeredWindow，
    //    即使文本几小时没变也持续占用主线程与 DWM 合成 → 系统判定窗口无响应。
    bool need_paint = false;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        const bool has_content = !content_.empty() || !status_.empty();
        const float target_alpha = has_content ? 1.0f : 0.0f;
        if (has_content != anim_has_content_) {
            anim_has_content_ = has_content;
            last_anim_ms_ = (int64_t)GetTickCount64();
            need_paint = true; // 内容有无切换 → 开始淡入/淡出
        } else if (std::fabs(alpha_ - target_alpha) > 0.0001f) {
            need_paint = true; // 动画进行中
        }
    }
    if (layout_dirty_.exchange(false)) need_paint = true; // 文本变化
    if (!need_paint) {
        // 完全静止：关掉定时器，主线程不再被唤醒（直到下次内容变化）
        if (timer_) {
            KillTimer(hwnd_, timer_);
            timer_ = 0;
        }
        return;
    }

    // 1. 淡入淡出动画：内容从无到有 → 渐入；从有到无 → 渐出
    {
        const int64_t now = (int64_t)GetTickCount64();
        const float dt = (float)(now - last_anim_ms_) / 1000.0f;
        last_anim_ms_ = now;
        const float target = anim_has_content_ ? 1.0f : 0.0f;
        const float duration = (target > alpha_) ? (float)style_.fade_in_ms / 1000.0f
                                                 : (float)style_.fade_out_ms / 1000.0f;
        if (duration > 0.0f && alpha_ != target) {
            const float step = dt / duration;
            if (target > alpha_) {
                alpha_ = std::min(target, alpha_ + step);
            } else {
                alpha_ = std::max(target, alpha_ - step);
            }
        }
    }

    // 2. 取当前文本（锁内拷贝）
    std::wstring full;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!content_.empty()) {
            full = content_;
        } else if (!status_.empty() && style_.show_status) {
            full = status_;
        }
    }

    // 3. 布局（文本变化时重建）
    if (layout_dirty_.exchange(false)) {
        rebuild_layout(full, (float)dib_w_, (float)dib_h_);
    }

    // 4. D2D 画到内存 DIB
    if (target_ && bg_brush_ && text_brush_ && (text_format_ || layout_)) {
        RECT rc = {0, 0, dib_w_, dib_h_};
        if (FAILED(target_->BindDC(mem_dc_, &rc))) return;
        target_->BeginDraw();
        target_->Clear(D2D1::ColorF(0, 0, 0, 0)); // 全透明

        if (style_.bg_color >> 24 != 0) {
            // 背景只画在文本实际区域（贴合文字，不铺满整个窗口）
            const float pad = style_.font_size * 0.22f;
            auto fill_bg = [&](float l, float t, float r, float b) {
                if (r <= l || b <= t) return;
                target_->FillRectangle(
                    D2D1::RectF(std::max(0.0f, l - pad), std::max(0.0f, t - pad),
                                std::min((float)dib_w_, r + pad),
                                std::min((float)dib_h_, b + pad)),
                    bg_brush_);
            };
            fill_bg(bg1_left_, bg1_top_, bg1_right_, bg1_bot_);
            // 布局失败时不再兜底铺满整窗（会把显示区撑到半个屏幕）；
            // 背景缺失仅影响这一帧的观感，下一帧布局恢复后自动正常
        }

        if (layout_) {
            draw_layout(layout_, 0.0f, 0.0f, true);
        } else if (!full.empty() && text_format_) {
            const float pad = style_.font_size * 0.4f;
            D2D1_RECT_F trc = D2D1::RectF(pad, pad, (float)dib_w_ - pad, (float)dib_h_ - pad);
            target_->DrawTextW(full.c_str(), (UINT32)full.size(), text_format_, trc,
                               text_brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        if (FAILED(target_->EndDraw())) return;
    }

    // 5. 上传分层窗口（SourceConstantAlpha = 动画 alpha）
    if (alpha_ <= 0.001f) return;
    HDC screen_dc = GetDC(nullptr);
    POINT pt = {0, 0};
    SIZE sz = {dib_w_, dib_h_};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, (BYTE)(alpha_ * style_.window_alpha * 255.0f), AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd_, screen_dc, nullptr, &sz, mem_dc_, &pt, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, screen_dc);
}

void SubtitleWindow::set_text(const std::string& text, size_t confirmed_offset) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        content_ = to_wide(text);
        confirmed_offset_ = confirmed_offset; // 与 content_ 同锁，避免与渲染线程竞争
    }
    layout_dirty_ = true;
    ensure_timer(); // 唤醒重绘（静止时定时器已关）
}

void SubtitleWindow::set_status(const std::string& status) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        status_ = to_wide(status);
    }
    layout_dirty_ = true;
    ensure_timer();
}

// 构建文本布局：
//   1. 行数超过 max_lines 时逐级缩小字号，直到最小字号（配置）
//   2. 缩到最小仍超行 → 滚动保留最后 max_lines 行（去掉最上面一行）
//   3. 滚动粘滞：已滚掉的前缀被记住，后续更新若仍以该前缀开头，
//      继续从滚动位置显示（识别重排导致文本变短时，滚上去的行不会回来）
void SubtitleWindow::rebuild_layout(const std::wstring& text, float w, float h) {
    if (layout_) { layout_->Release(); layout_ = nullptr; }
    bg1_left_ = bg1_top_ = bg1_right_ = bg1_bot_ = 0; // 无文本 → 无背景
    if (!dwrite_factory_ || !text_format_ || text.empty()) {
        scroll_skip_ = 0;
        scroll_prefix_.clear();
        return;
    }

    // 滚动粘滞：新文本以已滚掉的前缀开头 → 从滚动位置继续（滚掉的行不回来）
    std::wstring effective = text;
    size_t base_skip = 0;
    if (scroll_skip_ > 0 && text.size() >= scroll_skip_ &&
        text.compare(0, scroll_skip_, scroll_prefix_) == 0) {
        base_skip = scroll_skip_;
        effective = text.substr(scroll_skip_);
    } else {
        scroll_skip_ = 0; // 前缀变化（新句/内容重置）→ 从头显示
        scroll_prefix_.clear();
    }

    auto make = [&](const std::wstring& t) -> IDWriteTextLayout* {
        IDWriteTextLayout* l = nullptr;
        if (FAILED(dwrite_factory_->CreateTextLayout(t.c_str(), (UINT32)t.size(),
                                                     text_format_, w, h, &l))) {
            return nullptr;
        }
        return l;
    };
    // 记录文本实际包围盒（垂直居中后 GetMetrics 已含居中偏移）
    auto record_bg = [&](IDWriteTextLayout* l) {
        DWRITE_TEXT_METRICS m;
        if (l && SUCCEEDED(l->GetMetrics(&m))) {
            bg1_left_ = m.left;
            bg1_top_  = m.top;
            bg1_right_ = m.left + m.width;
            bg1_bot_  = m.top + m.height;
        }
    };

    const UINT32 max = (UINT32)std::max(1, style_.max_lines);
    const float min_size = std::max(8.0f, style_.min_font_size); // 最小字号（配置）

    IDWriteTextLayout* l = make(effective);
    if (!l) return;

    // 1. 字号自适应：超行逐级缩小，直到放得下或最小字号
    float size = style_.font_size;
    UINT32 lines = 0;
    for (int attempt = 0; attempt < 14; attempt++) {
        DWRITE_TEXT_RANGE range = {0, (UINT32)effective.size()};
        l->SetFontSize(size, range);
        lines = 0;
        l->GetLineMetrics(nullptr, 0, &lines);
        if (lines <= max || size <= min_size) break;
        size *= 0.92f;
    }
    if (lines <= max) {
        apply_interim_style(l, effective, base_skip);
        record_bg(l);
        layout_ = l;
        return; // 滚动状态保留（粘滞：即使本次不超行也不回退）
    }

    // 2. 最小字号仍超行 → 滚动保留最后 max 行（去掉最上面几行）
    std::vector<DWRITE_LINE_METRICS> metrics(lines);
    l->GetLineMetrics(metrics.data(), lines, &lines);
    UINT32 skip_chars = 0;
    for (UINT32 i = 0; i < lines - max; i++) {
        skip_chars += metrics[i].length;
    }
    while (skip_chars > 0 && skip_chars < effective.size() &&
           (effective[skip_chars] & 0xFC00) == 0xDC00) {
        skip_chars--;
    }
    l->Release();
    l = make(effective.substr(skip_chars));
    if (l) {
        DWRITE_TEXT_RANGE range = {0, (UINT32)effective.size() - skip_chars};
        l->SetFontSize(size, range);
        apply_interim_style(l, effective.substr(skip_chars), base_skip + skip_chars);
    }
    record_bg(l);
    layout_ = l;
    // 更新滚动状态（记住被滚掉的总前缀）
    const size_t total_skip = base_skip + skip_chars;
    scroll_skip_ = total_skip;
    scroll_prefix_ = text.substr(0, total_skip);
}

// 绘制 layout（描边 + 填充），(x, y) 为区域左上角
void SubtitleWindow::draw_layout(IDWriteTextLayout* l, float x, float y, bool stroke) {
    if (!l || !target_) return;
    if (stroke && style_.stroke_enabled && stroke_brush_ && style_.stroke_width > 0) {
        const float sw = (float)style_.stroke_width;
        const float offs[8][2] = {
            {-sw, 0}, {sw, 0}, {0, -sw}, {0, sw},
            {-sw, -sw}, {sw, -sw}, {-sw, sw}, {sw, sw},
        };
        for (auto& o : offs) {
            target_->DrawTextLayout(D2D1::Point2F(x + o[0], y + o[1]), l,
                                    stroke_brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }
    target_->DrawTextLayout(D2D1::Point2F(x, y), l, text_brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

// interim（未确认尾部）半透明样式：confirmed 偏移之后的部分
// base_offset：滚动后显示文本相对原始文本的偏移（confirmed_offset 需换算）
void SubtitleWindow::apply_interim_style(IDWriteTextLayout* l, const std::wstring& t,
                                         size_t base_offset) {
    if (!l || !interim_brush_ || confirmed_offset_ == std::string::npos) return;
    if (confirmed_offset_ > base_offset && confirmed_offset_ - base_offset < t.size()) {
        DWRITE_TEXT_RANGE range = {(UINT32)(confirmed_offset_ - base_offset),
                                   (UINT32)(t.size() - (confirmed_offset_ - base_offset))};
        l->SetDrawingEffect(interim_brush_, range);
    }
}

std::wstring SubtitleWindow::to_wide(const std::string& s) const {
    if (s.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w((size_t)n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

void SubtitleWindow::destroy() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    {
        std::lock_guard<std::mutex> lk(mtx_);
        content_.clear();
        status_.clear();
    }
    release_d2d();
    if (dib_) { DeleteObject(dib_); dib_ = nullptr; }
    if (mem_dc_) { DeleteDC(mem_dc_); mem_dc_ = nullptr; }
    if (dwrite_factory_) { dwrite_factory_->Release(); dwrite_factory_ = nullptr; }
    if (d2d_factory_)    { d2d_factory_->Release();    d2d_factory_    = nullptr; }
}
