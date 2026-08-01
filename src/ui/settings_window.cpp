#include "settings_window.h"

#include <commctrl.h>
#include <cstdio>
#include <algorithm>
#include <utility>

#include "config.h"
#include "audio/wasapi_capture.h"

// 控件 ID
enum {
    IDC_DEVICE      = 1001,
    IDC_REFRESH     = 1002,
    IDC_VAD_THRESH  = 1003,
    IDC_SILENCE     = 1004,
    IDC_CHUNK       = 1005,
    IDC_HOP         = 1006,
    IDC_FONT_SIZE   = 1007,
    IDC_FONT_COLOR  = 1008,
    IDC_BG_COLOR    = 1009,
    IDC_TOP         = 1010,
    IDC_CLICK_THRU  = 1011,
    IDC_WRITE_TEXT  = 1012,
    IDC_WRITE_SRT   = 1013,
    IDC_APPLY       = 1014,
    IDC_CLOSE       = 1015,
    IDC_MODEL_INFO  = 1016,
    IDC_WIN_X       = 1017,
    IDC_WIN_Y       = 1018,
    IDC_BG_ALPHA    = 1019,
};

static const int LABEL_W = 120, EDIT_W = 90, ROW_H = 30, PAD = 12;

SettingsWindow::SettingsWindow(Config& cfg,
                               std::function<void()> on_apply,
                               std::function<void()> on_reload_devices)
    : cfg_(cfg), on_apply_(std::move(on_apply)), on_reload_devices_(std::move(on_reload_devices)) {}

SettingsWindow::~SettingsWindow() {
    if (hwnd_) DestroyWindow(hwnd_);
    if (hfont_) { DeleteObject(hfont_); hfont_ = nullptr; }
}

static void add_label(HWND parent, int id, const wchar_t* text, int x, int y) {
    CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_RIGHT,
                  x, y + 4, LABEL_W, 20, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

static HWND add_edit(HWND parent, int id, const std::wstring& text, int x, int y) {
    return CreateWindowW(L"EDIT", text.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                         x, y, EDIT_W, 22, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
}

static std::wstring get_edit_text(HWND hwnd) {
    int n = GetWindowTextLengthW(hwnd);
    std::wstring s((size_t)n, L'\0');
    GetWindowTextW(hwnd, s.data(), n + 1);
    return s;
}


void SettingsWindow::populate_devices() {
    HWND combo = GetDlgItem(hwnd_, IDC_DEVICE);
    if (!combo) return;
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    auto devs = WasapiCapture::list_devices();
    for (const auto& [name, id] : devs) {
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)utf8_to_wide(name).c_str());
    }
    SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

void SettingsWindow::fill_fields() {
    auto set_edit = [&](int id, const std::wstring& v) {
        SetWindowTextW(GetDlgItem(hwnd_, id), v.c_str());
    };
    set_edit(IDC_VAD_THRESH, std::to_wstring((int)cfg_.vad_threshold_db));
    set_edit(IDC_SILENCE,    std::to_wstring(cfg_.silence_ms));
    set_edit(IDC_CHUNK,      std::to_wstring(cfg_.chunk_ms));
    set_edit(IDC_HOP,        std::to_wstring(cfg_.hop_ms));
    set_edit(IDC_FONT_SIZE,  std::to_wstring((int)cfg_.font_size));
    set_edit(IDC_FONT_COLOR, utf8_to_wide(cfg_.font_color));
    set_edit(IDC_BG_COLOR,   utf8_to_wide(cfg_.bg_color));
    set_edit(IDC_WIN_X,      std::to_wstring(cfg_.pos_x));
    set_edit(IDC_WIN_Y,      std::to_wstring(cfg_.pos_y));
    {
        // 背景透明度：从 bg_color 的 alpha 通道换算为百分比显示
        const auto c = parse_color(cfg_.bg_color).value_or(0xC0000000u);
        const int pct = (int)(((c >> 24) & 0xFF) * 100 / 255);
        set_edit(IDC_BG_ALPHA, std::to_wstring(pct));
    }
    CheckDlgButton(hwnd_, IDC_TOP,        cfg_.always_on_top ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd_, IDC_CLICK_THRU, cfg_.click_through ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd_, IDC_WRITE_TEXT, cfg_.write_text ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd_, IDC_WRITE_SRT,  cfg_.write_srt ? BST_CHECKED : BST_UNCHECKED);

    // 只显示文件名（完整路径太长会截断）
    auto basename = [](const std::string& p) {
        const size_t pos = p.find_last_of("\/");
        return pos == std::string::npos ? p : p.substr(pos + 1);
    };
    std::wstring info = L"模型: " + utf8_to_wide(basename(cfg_.model_path)) +
                        L"\n音频编码器: " + utf8_to_wide(basename(cfg_.mmproj_path));
    populate_devices();
}

void SettingsWindow::read_fields() {
    auto edit_int = [&](int id, int def) -> int {
        const std::wstring s = get_edit_text(GetDlgItem(hwnd_, id));
        return s.empty() ? def : _wtoi(s.c_str());
    };
    auto edit_str = [&](int id) {
        const std::wstring s = get_edit_text(GetDlgItem(hwnd_, id));
        int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string out((size_t)n - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, out.data(), n, nullptr, nullptr);
        return out;
    };

    cfg_.vad_threshold_db  = (float)edit_int(IDC_VAD_THRESH, (int)cfg_.vad_threshold_db);
    cfg_.silence_ms        = edit_int(IDC_SILENCE, cfg_.silence_ms);
    cfg_.chunk_ms          = std::max(500, edit_int(IDC_CHUNK, cfg_.chunk_ms));
    cfg_.hop_ms            = std::max(200, std::min(cfg_.chunk_ms, edit_int(IDC_HOP, cfg_.hop_ms)));
    cfg_.font_size         = (float)std::max(12, edit_int(IDC_FONT_SIZE, (int)cfg_.font_size));
    cfg_.font_color        = edit_str(IDC_FONT_COLOR);
    cfg_.bg_color          = edit_str(IDC_BG_COLOR);
    cfg_.pos_x             = std::max(0, std::min(100, edit_int(IDC_WIN_X, cfg_.pos_x)));
    cfg_.pos_y             = std::max(0, std::min(100, edit_int(IDC_WIN_Y, cfg_.pos_y)));
    {
        // 背景透明度：0-100% 写回 bg_color 的 alpha（保留原 RGB）
        int pct = edit_int(IDC_BG_ALPHA, 75);
        pct = std::max(0, std::min(100, pct));
        auto c = parse_color(cfg_.bg_color).value_or(0xC0000000u);
        char buf[16];
        snprintf(buf, sizeof(buf), "#%02X%06X", (unsigned)(pct * 255 / 100), c & 0xFFFFFF);
        cfg_.bg_color = buf;
    }
    cfg_.always_on_top     = IsDlgButtonChecked(hwnd_, IDC_TOP) == BST_CHECKED;
    cfg_.click_through     = IsDlgButtonChecked(hwnd_, IDC_CLICK_THRU) == BST_CHECKED;
    cfg_.write_text        = IsDlgButtonChecked(hwnd_, IDC_WRITE_TEXT) == BST_CHECKED;
    cfg_.write_srt         = IsDlgButtonChecked(hwnd_, IDC_WRITE_SRT) == BST_CHECKED;
}

void SettingsWindow::apply() {
    read_fields();
    cfg_.save(cfg_.path());
    if (on_apply_) on_apply_();
}

LRESULT CALLBACK SettingsWindow::wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    SettingsWindow* self = (SettingsWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
            self = (SettingsWindow*)cs->lpCreateParams;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
            self->hwnd_ = hwnd;
            self->fill_fields();
            return 0;
        }
        case WM_COMMAND: {
            if (!self) return 0;
            const int id = LOWORD(wp);
            switch (id) {
                case IDC_APPLY:
                    self->apply();
                    return 0;
                case IDC_CLOSE:
                    DestroyWindow(hwnd);
                    return 0;
                case IDC_REFRESH:
                    self->populate_devices();
                    return 0;
            }
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            self->done_ = true;
            self->hwnd_ = nullptr;
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

void SettingsWindow::run() {
    const wchar_t* cls = L"LiveSubSettingsWindow";
    HINSTANCE hinst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hinst;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = cls;
    RegisterClassExW(&wc);

    const int W = LABEL_W + EDIT_W + PAD * 3 + 330; // 560：容纳说明文字与刷新按钮
    const int H = 13 * ROW_H + 90;
    hwnd_ = CreateWindowExW(0, cls, L"LiveSub 设置", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                            CW_USEDEFAULT, CW_USEDEFAULT, W, H,
                            nullptr, nullptr, hinst, this);
    if (!hwnd_) return;

    // 控件创建
    HWND h = hwnd_;
    int y = PAD;
    auto row = [&]() { return y; };
    auto next = [&]() { int r = y; y += ROW_H; return r; };

    add_label(h, 0, L"麦克风", PAD, next());
    CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                  LABEL_W + PAD, y - ROW_H + 2, EDIT_W + 150, 200, h,
                  (HMENU)(INT_PTR)IDC_DEVICE, hinst, nullptr);
    CreateWindowW(L"BUTTON", L"刷新", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                  LABEL_W + EDIT_W + 170, y - ROW_H + 1, 60, 22, h,
                  (HMENU)(INT_PTR)IDC_REFRESH, hinst, nullptr);

    add_label(h, 0, L"VAD 阈值(dB)", PAD, next());
    add_edit(h, IDC_VAD_THRESH, L"", LABEL_W + PAD, y - ROW_H + 1);
    add_label(h, 0, L"句末静音(ms)", PAD, next());
    add_edit(h, IDC_SILENCE, L"", LABEL_W + PAD, y - ROW_H + 1);
    add_label(h, 0, L"窗口(ms)", PAD, next());
    add_edit(h, IDC_CHUNK, L"", LABEL_W + PAD, y - ROW_H + 1);
    add_label(h, 0, L"步长(ms)", PAD, next());
    add_edit(h, IDC_HOP, L"", LABEL_W + PAD, y - ROW_H + 1);
    add_label(h, 0, L"字号", PAD, next());
    add_edit(h, IDC_FONT_SIZE, L"", LABEL_W + PAD, y - ROW_H + 1);
    add_label(h, 0, L"文字颜色", PAD, next());
    add_edit(h, IDC_FONT_COLOR, L"", LABEL_W + PAD, y - ROW_H + 1);
    add_label(h, 0, L"背景颜色", PAD, next());
    add_edit(h, IDC_BG_COLOR, L"", LABEL_W + PAD, y - ROW_H + 1);
    add_label(h, 0, L"背景透明度%", PAD, next());
    add_edit(h, IDC_BG_ALPHA, L"", LABEL_W + PAD, y - ROW_H + 1);
    CreateWindowW(L"STATIC", L"0=全透明 100=不透明（默认75）", WS_CHILD | WS_VISIBLE | SS_LEFT,
                  LABEL_W + EDIT_W + PAD + 8, y - ROW_H + 4, 300, 18, h,
                  nullptr, GetModuleHandleW(nullptr), nullptr);
    add_label(h, 0, L"位置 %", PAD, next());
    add_edit(h, IDC_WIN_X, L"", LABEL_W + PAD, y - ROW_H + 1);
    CreateWindowW(L"STATIC", L"屏幕百分比 0-100；50=居中（默认 50, 85 居中偏下）",
                  WS_CHILD | WS_VISIBLE | SS_LEFT,
                  LABEL_W + EDIT_W + PAD + 8, y - ROW_H + 4, 320, 18, h,
                  nullptr, GetModuleHandleW(nullptr), nullptr);
    add_edit(h, IDC_WIN_Y, L"", LABEL_W + EDIT_W + PAD + 130, y - ROW_H + 1);

    CreateWindowW(L"BUTTON", L"置顶", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                  PAD, next(), 100, 22, h, (HMENU)(INT_PTR)IDC_TOP, hinst, nullptr);
    CreateWindowW(L"BUTTON", L"点击穿透", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                  PAD + 110, y - ROW_H + 2, 120, 22, h, (HMENU)(INT_PTR)IDC_CLICK_THRU, hinst, nullptr);
    CreateWindowW(L"BUTTON", L"写文本文件", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                  PAD, next(), 120, 22, h, (HMENU)(INT_PTR)IDC_WRITE_TEXT, hinst, nullptr);
    CreateWindowW(L"BUTTON", L"写 SRT", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                  PAD + 130, y - ROW_H + 2, 100, 22, h, (HMENU)(INT_PTR)IDC_WRITE_SRT, hinst, nullptr);

    CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
                  PAD, next(), W - PAD * 3, 44, h, (HMENU)(INT_PTR)IDC_MODEL_INFO, hinst, nullptr);
    CreateWindowW(L"BUTTON", L"应用并保存", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                  PAD, y + 4, 120, 26, h, (HMENU)(INT_PTR)IDC_APPLY, hinst, nullptr);
    CreateWindowW(L"BUTTON", L"关闭", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                  PAD + 130, y + 4, 80, 26, h, (HMENU)(INT_PTR)IDC_CLOSE, hinst, nullptr);

    fill_fields();

    // 设置窗整体使用仿宋字体
    hfont_ = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         DEFAULT_QUALITY, DEFAULT_PITCH, L"仿宋");
    if (hfont_) {
        EnumChildWindows(hwnd_, [](HWND c, LPARAM lp) -> BOOL {
            SendMessageW(c, WM_SETFONT, (WPARAM)lp, TRUE);
            return TRUE;
        }, (LPARAM)hfont_);
    }

    ShowWindow(hwnd_, SW_SHOW);

    // 模态消息循环
    MSG msg;
    while (!done_ && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}
