#include "tray_icon.h"

#include <cmath>
#include <cstdint>

#include <shellapi.h>

// 托盘图标：隐藏宿主窗口接收 Shell_NotifyIcon 回调消息

static const UINT WM_TRAY = WM_APP + 1;

HICON TrayIcon::make_icon(COLORREF color) {
    // 16x16 32bpp DIB：圆形（带 alpha）+ 白色字幕横线
    const int W = 16, H = 16;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!bmp || !bits) { if (bmp) DeleteObject(bmp); return nullptr; }

    const uint32_t* px = (uint32_t*)bits;
    const uint8_t cr = GetRValue(color), cg = GetGValue(color), cb = GetBValue(color);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            const int dx = x - 8, dy = y - 8;
            const float r2 = (float)(dx * dx + dy * dy);
            if (r2 > 8.5f * 8.5f) {
                ((uint32_t*)bits)[y * W + x] = 0x00000000; // 全透明
                continue;
            }
            // 抗锯齿边缘
            uint8_t a = 255;
            if (r2 > 7.5f * 7.5f) {
                a = (uint8_t)(255.0f * (8.5f - std::sqrt(r2)) / 1.0f);
            }
            // 白色字幕横线（y=5,8,11，x=4..12）
            bool line = (y >= 4 && y <= 6 && x >= 3 && x <= 13) ||
                        (y >= 7 && y <= 9 && x >= 3 && x <= 13) ||
                        (y >= 10 && y <= 12 && x >= 3 && x <= 13);
            uint8_t r = line ? 255 : cr;
            uint8_t g = line ? 255 : cg;
            uint8_t b = line ? 255 : cb;
            ((uint32_t*)bits)[y * W + x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
    }

    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmColor = bmp;
    ii.hbmMask = CreateBitmap(W, H, 1, 1, nullptr);
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(ii.hbmMask);
    DeleteObject(bmp);
    (void)px;
    return icon;
}

bool TrayIcon::create(const std::wstring& tip) {
    // 隐藏宿主窗口
    const wchar_t* cls = L"LiveSubTrayHost";
    HINSTANCE hinst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = host_proc;
    wc.hInstance = hinst;
    wc.lpszClassName = cls;
    RegisterClassExW(&wc);
    host_ = CreateWindowExW(0, cls, L"", WS_POPUP, 0, 0, 0, 0, HWND_MESSAGE,
                            nullptr, hinst, this);
    if (!host_) return false;

    icons_[0] = make_icon(RGB(128, 128, 128)); // Loading 灰
    icons_[1] = make_icon(RGB(70, 130, 230));  // Ready 蓝
    icons_[2] = make_icon(RGB(60, 200, 90));   // Listening 绿
    icons_[3] = make_icon(RGB(220, 60, 60));   // Error 红

    nid_ = {};
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = host_;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = WM_TRAY;
    nid_.hIcon = icons_[0];
    wcsncpy(nid_.szTip, tip.c_str(), 127);
    added_ = Shell_NotifyIconW(NIM_ADD, &nid_) != FALSE;
    if (!added_) return false;
    set_state(State::Loading, tip);
    return true;
}

void TrayIcon::set_state(State s, const std::wstring& tip) {
    state_ = s;
    if (!added_) return;
    nid_.uFlags = NIF_ICON | NIF_TIP;
    nid_.hIcon = icons_[(int)s];
    if (!tip.empty()) {
        wcsncpy(nid_.szTip, tip.c_str(), 127);
    }
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

void TrayIcon::show_menu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"设置…");
    AppendMenuW(menu, MF_STRING, 2, L"显示/隐藏字幕");
    AppendMenuW(menu, MF_STRING, 3, recording_ ? L"结束记录讲话稿" : L"开始记录讲话稿");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 4, L"退出");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(host_);
    const int cmd = (int)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                                        pt.x, pt.y, 0, host_, nullptr);
    DestroyMenu(menu);
    switch (cmd) {
        case 1: if (on_open_settings) on_open_settings(); break;
        case 2: if (on_toggle_window) on_toggle_window(); break;
        case 3: if (on_toggle_record) on_toggle_record(); break;
        case 4: if (on_quit) on_quit(); break;
    }
}

LRESULT CALLBACK TrayIcon::host_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    TrayIcon* self = (TrayIcon*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_CREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        self = (TrayIcon*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        return 0;
    }
    if (msg == WM_TRAY) {
        if (!self) return 0;
        const UINT ev = LOWORD(lp);
        if (ev == WM_RBUTTONUP || ev == WM_CONTEXTMENU) {
            self->show_menu();
        } else if (ev == WM_LBUTTONDBLCLK) {
            if (self->on_open_settings) self->on_open_settings();
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void TrayIcon::destroy() {
    if (added_) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        added_ = false;
    }
    if (host_) {
        DestroyWindow(host_);
        host_ = nullptr;
    }
    for (auto& ic : icons_) {
        if (ic) { DestroyIcon(ic); ic = nullptr; }
    }
}
