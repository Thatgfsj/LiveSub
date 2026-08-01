#include "settings_window.h"

#include <commctrl.h>
#include <cstdio>
#include <algorithm>
#include <utility>
#include <vector>

#include "config.h"
#include "audio/wasapi_capture.h"

// 控件 ID（按页分区，便于切换时批量显隐）
enum {
    // Tab 控件
    IDC_TAB = 1000,

    // 页1 字幕显示
    IDC_FONT_SIZE = 1101, IDC_FONT_COLOR, IDC_BG_COLOR, IDC_BG_ALPHA,
    IDC_MAX_LINES, IDC_STROKE, IDC_STROKE_COLOR, IDC_STROKE_W,
    IDC_WIN_X, IDC_WIN_Y, IDC_TOP, IDC_CLICK_THRU,

    // 页2 识别
    IDC_VAD_THRESH = 1201, IDC_SILENCE, IDC_CHUNK, IDC_HOP,
    IDC_MODEL_BIG, IDC_MODEL_SMALL, IDC_MODEL_INFO,

    // 页3 音频
    IDC_DEVICE = 1301, IDC_REFRESH, IDC_MIC_TRACK, IDC_PC_TRACK,

    // 页4 输出
    IDC_WRITE_TEXT = 1401, IDC_WRITE_SRT,

    // 底部按钮
    IDC_APPLY = 1501, IDC_CLOSE,
};

// DPI 缩放：进程已声明 DPI 感知（见 livesub.manifest），
// 所有界面尺寸按屏幕 DPI 换算（96 基准），避免在 125%/150% 缩放下文字模糊/控件错位
static int S(int v) { return MulDiv(v, GetDpiForSystem(), 96); }
// 布局常量：运行时求值（静态初始化期调用 Win32 API 不可靠，
// GetDpiForSystem 若返回 0 会导致所有尺寸归零、控件挤没）
static int LABEL_W() { return S(150); }
static int EDIT_W()  { return S(80); }
static int PAD()     { return S(12); }
static int PAGE_TOP = 0;      // 内容区起点 Y（run() 中 TCM_ADJUSTRECT 计算）
// BTN_Y 改为 run() 局部变量

SettingsWindow::SettingsWindow(Config& cfg,
                               std::function<void()> on_apply,
                               std::function<void()> on_reload_devices)
    : cfg_(cfg), on_apply_(std::move(on_apply)), on_reload_devices_(std::move(on_reload_devices)) {}

SettingsWindow::~SettingsWindow() {
    if (hwnd_) DestroyWindow(hwnd_);
    if (hfont_) { DeleteObject(hfont_); hfont_ = nullptr; }
}

// ---- 页控件注册表 ----
// 问题：label/hint 创建时无 ID（HMENU=nullptr），show_page 按 ID 区间遍历永远找不到它们，
// 导致所有页的标签/说明文字同时可见、互相重叠。改为创建时按"当前页"登记句柄。
static std::vector<std::pair<HWND, int>> g_page_ctls; // (控件, 所属页)
static int g_cur_page = 0;                            // 当前创建控件所属页（run() 布局段设置）

static void register_ctl(HWND c) {
    if (c) g_page_ctls.emplace_back(c, g_cur_page);
}

// label（右对齐，宽度足够容纳中文长标签，避免文字被裁）
static void add_label(HWND parent, const wchar_t* text, int x, int y) {
    HWND c = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_RIGHT,
                           x, y + S(4), LABEL_W(), S(20), parent, nullptr,
                           GetModuleHandleW(nullptr), nullptr);
    register_ctl(c);
}

static HWND add_edit(HWND parent, int id, int x, int y) {
    return CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                         x, y, EDIT_W(), S(22), parent, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
}

// 说明文字（灰色小字）：宽度按文本实际测量自适应（不截断、不超窗）
static HWND add_hint(HWND parent, const wchar_t* text, int x, int y) {
    // 用与界面一致的仿宋字体测量文本宽度
    HDC dc = GetDC(parent);
    HFONT f = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          DEFAULT_QUALITY, DEFAULT_PITCH, L"仿宋");
    HGDIOBJ old = SelectObject(dc, f);
    RECT rc = {0, 0, 0, 0};
    DrawTextW(dc, text, -1, &rc, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old);
    DeleteObject(f);
    ReleaseDC(parent, dc);
    RECT cr;
    GetClientRect(parent, &cr);
    int w = (rc.right - rc.left) + S(4);
    const int maxw = cr.right - S(8) - x;
    if (w > maxw) w = maxw; // 超窗时截断到窗口内
    HWND c = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                         x, y + S(4), w, S(18), parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    register_ctl(c);
    return c;
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
        HWND c = GetDlgItem(hwnd_, id);
        if (c) SetWindowTextW(c, v.c_str());
    };
    // 页1 字幕
    set_edit(IDC_FONT_SIZE, std::to_wstring((int)cfg_.font_size));
    set_edit(IDC_FONT_COLOR, utf8_to_wide(cfg_.font_color));
    set_edit(IDC_BG_COLOR, utf8_to_wide(cfg_.bg_color));
    {
        const auto c = parse_color(cfg_.bg_color).value_or(0xC0000000u);
        const int pct = (int)(((c >> 24) & 0xFF) * 100 / 255);
        set_edit(IDC_BG_ALPHA, std::to_wstring(pct));
    }
    set_edit(IDC_MAX_LINES, std::to_wstring(cfg_.max_lines));
    set_edit(IDC_STROKE_COLOR, utf8_to_wide(cfg_.stroke_color));
    set_edit(IDC_STROKE_W, std::to_wstring(cfg_.stroke_width));
    set_edit(IDC_WIN_X, std::to_wstring(cfg_.pos_x));
    set_edit(IDC_WIN_Y, std::to_wstring(cfg_.pos_y));
    CheckDlgButton(hwnd_, IDC_STROKE, cfg_.stroke_enabled ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd_, IDC_TOP, cfg_.always_on_top ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd_, IDC_CLICK_THRU, cfg_.click_through ? BST_CHECKED : BST_UNCHECKED);

    // 页2 识别
    set_edit(IDC_VAD_THRESH, std::to_wstring((int)cfg_.vad_threshold_db));
    set_edit(IDC_SILENCE, std::to_wstring(cfg_.silence_ms));
    set_edit(IDC_CHUNK, std::to_wstring(cfg_.chunk_ms));
    set_edit(IDC_HOP, std::to_wstring(cfg_.hop_ms));
    if (cfg_.model_size == "small") {
        CheckRadioButton(hwnd_, IDC_MODEL_BIG, IDC_MODEL_SMALL, IDC_MODEL_SMALL);
    } else {
        CheckRadioButton(hwnd_, IDC_MODEL_BIG, IDC_MODEL_SMALL, IDC_MODEL_BIG);
    }

    // 页3 音频
    CheckDlgButton(hwnd_, IDC_MIC_TRACK, cfg_.mic_enabled ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd_, IDC_PC_TRACK, cfg_.pc_enabled ? BST_CHECKED : BST_UNCHECKED);

    // 页4 输出
    CheckDlgButton(hwnd_, IDC_WRITE_TEXT, cfg_.write_text ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd_, IDC_WRITE_SRT, cfg_.write_srt ? BST_CHECKED : BST_UNCHECKED);

    // 模型信息（只显示文件名，完整路径太长会截断）
    auto basename = [](const std::string& p) {
        const size_t pos = p.find_last_of("\\/");
        return pos == std::string::npos ? p : p.substr(pos + 1);
    };
    std::wstring info = L"模型: " + utf8_to_wide(basename(cfg_.model_path)) +
                        L"\n音频编码器: " + utf8_to_wide(basename(cfg_.mmproj_path));
    HWND mi = GetDlgItem(hwnd_, IDC_MODEL_INFO);
    if (mi) SetWindowTextW(mi, info.c_str());

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

    // 页1 字幕
    cfg_.font_size         = (float)std::max(12, edit_int(IDC_FONT_SIZE, (int)cfg_.font_size));
    cfg_.font_color        = edit_str(IDC_FONT_COLOR);
    cfg_.bg_color          = edit_str(IDC_BG_COLOR);
    cfg_.pos_x             = std::max(0, std::min(GetSystemMetrics(SM_CXSCREEN), edit_int(IDC_WIN_X, cfg_.pos_x)));
    cfg_.pos_y             = std::max(0, std::min(GetSystemMetrics(SM_CYSCREEN), edit_int(IDC_WIN_Y, cfg_.pos_y)));
    {
        int pct = edit_int(IDC_BG_ALPHA, 75);
        pct = std::max(0, std::min(100, pct));
        auto c = parse_color(cfg_.bg_color).value_or(0xC0000000u);
        char buf[16];
        snprintf(buf, sizeof(buf), "#%02X%06X", (unsigned)(pct * 255 / 100), c & 0xFFFFFF);
        cfg_.bg_color = buf;
    }
    cfg_.max_lines = std::max(1, std::min(6, edit_int(IDC_MAX_LINES, cfg_.max_lines)));
    cfg_.stroke_enabled = IsDlgButtonChecked(hwnd_, IDC_STROKE) == BST_CHECKED;
    cfg_.stroke_color   = edit_str(IDC_STROKE_COLOR);
    cfg_.stroke_width   = std::max(0, std::min(8, edit_int(IDC_STROKE_W, cfg_.stroke_width)));
    cfg_.always_on_top     = IsDlgButtonChecked(hwnd_, IDC_TOP) == BST_CHECKED;
    cfg_.click_through     = IsDlgButtonChecked(hwnd_, IDC_CLICK_THRU) == BST_CHECKED;

    // 页2 识别
    cfg_.vad_threshold_db  = (float)edit_int(IDC_VAD_THRESH, (int)cfg_.vad_threshold_db);
    cfg_.silence_ms        = edit_int(IDC_SILENCE, cfg_.silence_ms);
    cfg_.chunk_ms          = std::max(500, edit_int(IDC_CHUNK, cfg_.chunk_ms));
    cfg_.hop_ms            = std::max(200, std::min(cfg_.chunk_ms, edit_int(IDC_HOP, cfg_.hop_ms)));
    if (IsDlgButtonChecked(hwnd_, IDC_MODEL_SMALL) == BST_CHECKED) {
        cfg_.model_size = "small";
    } else {
        cfg_.model_size = "large";
    }

    // 页3 音频
    cfg_.mic_enabled = IsDlgButtonChecked(hwnd_, IDC_MIC_TRACK) == BST_CHECKED;
    cfg_.pc_enabled  = IsDlgButtonChecked(hwnd_, IDC_PC_TRACK) == BST_CHECKED;

    // 页4 输出
    cfg_.write_text = IsDlgButtonChecked(hwnd_, IDC_WRITE_TEXT) == BST_CHECKED;
    cfg_.write_srt  = IsDlgButtonChecked(hwnd_, IDC_WRITE_SRT) == BST_CHECKED;
}

void SettingsWindow::apply() {
    read_fields();
    cfg_.save(cfg_.path());
    if (on_apply_) on_apply_();
}

// 切换 Tab 页：只显示当前页的控件
void SettingsWindow::show_page(int page) {
    const int pages[4][2] = {
        {IDC_FONT_SIZE, IDC_CLICK_THRU},
        {IDC_VAD_THRESH, IDC_MODEL_INFO},
        {IDC_DEVICE, IDC_PC_TRACK},
        {IDC_WRITE_TEXT, IDC_WRITE_SRT},
    };
    for (int i = 0; i < 4; i++) {
        const bool vis = (i == page);
        for (int id = pages[i][0]; id <= pages[i][1]; id++) {
            HWND c = GetDlgItem(hwnd_, id);
            if (c) ShowWindow(c, vis ? SW_SHOW : SW_HIDE);
        }
    }
    // label/hint（无 ID）：按句柄容器同步显隐
    for (auto& [c, pg] : g_page_ctls) {
        ShowWindow(c, pg == page ? SW_SHOW : SW_HIDE);
    }
}

// 窗口尺寸变化：内容布局保持不变，只拉宽 Tab、把底部按钮贴到窗口底部
// （等比缩放控件会导致间距错乱，因此不做）
void SettingsWindow::on_size(int w, int h) {
    HWND tab = GetDlgItem(hwnd_, IDC_TAB);
    if (tab) SetWindowPos(tab, nullptr, 0, 0, w - PAD() * 2, S(26), SWP_NOMOVE | SWP_NOZORDER);
    HWND a = GetDlgItem(hwnd_, IDC_APPLY);
    HWND c = GetDlgItem(hwnd_, IDC_CLOSE);
    if (a) SetWindowPos(a, nullptr, PAD(), h - S(34), S(120), S(26), SWP_NOZORDER);
    if (c) SetWindowPos(c, nullptr, PAD() + S(130), h - S(34), S(80), S(26), SWP_NOZORDER);
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
        case WM_NOTIFY: {
            NMHDR* h = (NMHDR*)lp;
            if (h && h->code == TCN_SELCHANGE && h->idFrom == IDC_TAB) {
                const int sel = (int)SendMessageW(h->hwndFrom, TCM_GETCURSEL, 0, 0);
                if (self) self->show_page(sel);
            }
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_SIZE: {
            if (self) self->on_size(LOWORD(lp), HIWORD(lp));
            return 0;
        }
        case WM_GETMINMAXINFO: {
            // 最小尺寸 = 初始窗口尺寸（缩到初始为止，内容不会挤乱）
            // 注意：窗口创建期间（WM_CREATE 前）就会收到此消息，self 可能为空
            MINMAXINFO* mmi = (MINMAXINFO*)lp;
            if (self && self->min_w_ > 0 && self->min_h_ > 0) {
                mmi->ptMinTrackSize.x = self->min_w_;
                mmi->ptMinTrackSize.y = self->min_h_;
            }
            return 0;
        }
        case WM_DESTROY:
            if (self) {
                self->done_ = true;
                self->hwnd_ = nullptr;
            }
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

void SettingsWindow::run() {
    // Tab 控件需要 comctl32 初始化（否则 WC_TABCONTROLW 创建失败，界面崩坏）
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_TAB_CLASSES };
    InitCommonControlsEx(&icc);

    const wchar_t* cls = L"LiveSubSettingsWindow";
    HINSTANCE hinst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hinst;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = cls;
    RegisterClassExW(&wc);

    const int W = S(640);
    const int btn_y = S(412);
    const int H = btn_y + S(40); // 内容 + 底部按钮 + 边距
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                        WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX;
    RECT wrc = {0, 0, W, H};
    AdjustWindowRectEx(&wrc, style, FALSE, 0);
    hwnd_ = CreateWindowExW(0, cls, L"LiveSub 设置", style,
                            CW_USEDEFAULT, CW_USEDEFAULT,
                            wrc.right - wrc.left, wrc.bottom - wrc.top,
                            nullptr, nullptr, hinst, this);
    if (!hwnd_) return;

    // 记录初始窗口总尺寸（缩放最小限制）
    {
        RECT wr;
        GetWindowRect(hwnd_, &wr);
        min_w_ = wr.right - wr.left;
        min_h_ = wr.bottom - wr.top;
    }

    HWND h = hwnd_;

    // 每次打开清空页控件注册表（句柄会随窗口销毁失效）
    g_page_ctls.clear();
    g_cur_page = 0;

    // ---- Tab 控件（4 页） ----
    HWND tab = CreateWindowW(WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                             PAD(), S(8), W - PAD() * 2, S(26), h, (HMENU)(INT_PTR)IDC_TAB, hinst, nullptr);
    {
        TCITEMW ti = {};
        ti.mask = TCIF_TEXT;
        const wchar_t* names[] = {L"字幕显示", L"识别", L"音频", L"输出"};
        for (int i = 0; i < 4; i++) {
            ti.pszText = (LPWSTR)names[i];
            SendMessageW(tab, TCM_INSERTITEM, i, (LPARAM)&ti);
        }
    }
    // 内容区起点：TCM_ADJUSTRECT 计算 Tab 的实际显示区域（官方推荐做法）
    {
        RECT tr;
        GetClientRect(tab, &tr);
        SendMessageW(tab, TCM_ADJUSTRECT, FALSE, (LPARAM)&tr);
        PAGE_TOP = tr.top + S(2);
    }

    const int c1 = PAD() + S(8);                    // 列1 label X
    const int c1e = c1 + LABEL_W() + S(6);          // 列1 edit X
    const int c2 = S(340);                        // 列2 label X
    const int c2e = c2 + LABEL_W() + S(6);          // 列2 edit X
    auto row1 = [&](int i) { return PAGE_TOP + i * S(34); };

    // ================= 页1 字幕显示（两列） =================
    g_cur_page = 0;
    // 列1 行尾不放说明文字：列1 hint 起点(≈264)会侵入列2 区域(≥340)造成重叠，
    // 关键范围说明已并入 label（如"透明度%(0-100)"）
    add_label(h, L"字号", c1, row1(0));
    add_edit(h, IDC_FONT_SIZE, c1e, row1(0));
    add_label(h, L"文字颜色", c1, row1(1));
    add_edit(h, IDC_FONT_COLOR, c1e, row1(1));
    add_label(h, L"背景颜色", c1, row1(2));
    add_edit(h, IDC_BG_COLOR, c1e, row1(2));
    add_label(h, L"透明度%(0-100)", c1, row1(3));
    add_edit(h, IDC_BG_ALPHA, c1e, row1(3));
    add_label(h, L"行数(1-6)", c1, row1(4));
    add_edit(h, IDC_MAX_LINES, c1e, row1(4));
    CreateWindowW(L"BUTTON", L"文字描边", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                  c1, row1(5), S(96), S(22), h, (HMENU)(INT_PTR)IDC_STROKE, hinst, nullptr);
    add_edit(h, IDC_STROKE_COLOR, c1e, row1(5));
    add_label(h, L"描边粗(1-8)", c1, row1(6));
    add_edit(h, IDC_STROKE_W, c1e, row1(6));

    add_label(h, L"位置X(中心)", c2, row1(0));
    add_edit(h, IDC_WIN_X, c2e, row1(0));
    add_label(h, L"位置Y(中心)", c2, row1(1));
    add_edit(h, IDC_WIN_Y, c2e, row1(1));
    add_hint(h, L"X=960 居中，Y=900 靠底", c2 + S(8), row1(3));
    CreateWindowW(L"BUTTON", L"置顶", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                  c2, row1(2), S(100), S(22), h, (HMENU)(INT_PTR)IDC_TOP, hinst, nullptr);
    CreateWindowW(L"BUTTON", L"点击穿透", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                  c2 + S(110), row1(2), S(110), S(22), h, (HMENU)(INT_PTR)IDC_CLICK_THRU, hinst, nullptr);
    add_hint(h, L"穿透后鼠标可正常操作直播软件", c2 + S(8), row1(4));

    // ================= 页2 识别（单列） =================
    g_cur_page = 1;
    add_label(h, L"VAD 阈值(dB)", c1, row1(0));
    add_edit(h, IDC_VAD_THRESH, c1e, row1(0));
    add_hint(h, L"说话触发门限（默认-52），越接近0越难触发", c1e + EDIT_W() + S(8), row1(0));
    add_label(h, L"句末静音(ms)", c1, row1(1));
    add_edit(h, IDC_SILENCE, c1e, row1(1));
    add_label(h, L"识别窗口(ms)", c1, row1(2));
    add_edit(h, IDC_CHUNK, c1e, row1(2));
    add_label(h, L"窗口步长(ms)", c1, row1(3));
    add_edit(h, IDC_HOP, c1e, row1(3));
    add_label(h, L"模型", c1, row1(4));
    CreateWindowW(L"BUTTON", L"大模型 1.7B（更准）", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                  c1e, row1(4), S(150), S(22), h, (HMENU)(INT_PTR)IDC_MODEL_BIG, hinst, nullptr);
    CreateWindowW(L"BUTTON", L"小模型 0.6B（更快）", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                  c1e + S(160), row1(4), S(150), S(22), h, (HMENU)(INT_PTR)IDC_MODEL_SMALL, hinst, nullptr);
    add_hint(h, L"大≈2.8GB / 小≈1.1GB，保存后重启生效", c1e + EDIT_W() + S(8), row1(5));
    CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
                  c1, row1(6), W - PAD() * 2 - c1, S(44), h, (HMENU)(INT_PTR)IDC_MODEL_INFO, hinst, nullptr);

    // ================= 页3 音频（单列） =================
    g_cur_page = 2;
    add_label(h, L"麦克风", c1, row1(0));
    CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                  c1e, row1(0), EDIT_W() + S(200), S(200), h,
                  (HMENU)(INT_PTR)IDC_DEVICE, hinst, nullptr);
    CreateWindowW(L"BUTTON", L"刷新", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                  c1e + EDIT_W() + S(210), row1(0), S(56), S(22), h,
                  (HMENU)(INT_PTR)IDC_REFRESH, hinst, nullptr);
    CreateWindowW(L"BUTTON", L"麦克风字幕", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                  c1, row1(2), S(120), S(22), h, (HMENU)(INT_PTR)IDC_MIC_TRACK, hinst, nullptr);
    CreateWindowW(L"BUTTON", L"电脑字幕", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                  c1 + S(130), row1(2), S(110), S(22), h, (HMENU)(INT_PTR)IDC_PC_TRACK, hinst, nullptr);
    add_hint(h, L"电脑字幕=识别电脑播放的声音（视频/直播）", c1 + S(250), row1(2));
    add_hint(h, L"两条字幕共用同一展示框，一般不同时开启；也可在托盘右键快速切换",
             c1, row1(3));

    // ================= 页4 输出（单列） =================
    g_cur_page = 3;
    CreateWindowW(L"BUTTON", L"写文本文件", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                  c1, row1(0), S(120), S(22), h, (HMENU)(INT_PTR)IDC_WRITE_TEXT, hinst, nullptr);
    add_hint(h, L"每次定稿句追加到 subtitles.txt", c1 + S(130), row1(0));
    CreateWindowW(L"BUTTON", L"写 SRT 字幕", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                  c1, row1(1), S(120), S(22), h, (HMENU)(INT_PTR)IDC_WRITE_SRT, hinst, nullptr);
    add_hint(h, L"带时间轴的 subtitles.srt", c1 + S(130), row1(1));
    add_hint(h, L"讲话稿记录（按时间命名存到桌面）在托盘菜单开启", c1, row1(3));

    // ---- 底部按钮 ----
    CreateWindowW(L"BUTTON", L"应用并保存", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                  PAD(), btn_y, S(120), S(26), h, (HMENU)(INT_PTR)IDC_APPLY, hinst, nullptr);
    CreateWindowW(L"BUTTON", L"关闭", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                  PAD() + S(130), btn_y, S(80), S(26), h, (HMENU)(INT_PTR)IDC_CLOSE, hinst, nullptr);

    fill_fields();

    // 设置窗整体使用仿宋字体
    hfont_ = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         DEFAULT_QUALITY, DEFAULT_PITCH, L"仿宋");
    if (hfont_) {
        EnumChildWindows(hwnd_, [](HWND c, LPARAM lp) -> BOOL {
            SendMessageW(c, WM_SETFONT, (WPARAM)lp, TRUE);
            return TRUE;
        }, (LPARAM)hfont_);
    }

    // 初始显示第 1 页（其余隐藏）
    show_page(0);

    ShowWindow(hwnd_, SW_SHOW);

    // 模态消息循环
    MSG msg;
    while (!done_ && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}
