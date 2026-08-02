#include "config.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <algorithm>

#include <windows.h>

std::string exe_dir() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring w(buf);
    const size_t pos = w.find_last_of(L"\\/");
    if (pos != std::wstring::npos) w.resize(pos);
    return wide_to_utf8(w);
}

std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w((size_t)n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

std::string wide_to_utf8(const std::wstring& s) {
    if (s.empty()) return "";
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out((size_t)n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, out.data(), n, nullptr, nullptr);
    return out;
}

static bool file_exists(const std::string& p) {
    FILE* f = fopen(p.c_str(), "rb");
    if (f) { fclose(f); return true; }
    return false;
}

std::string resolve_path(const std::string& p) {
    if (p.empty()) return p;
    // 绝对路径（盘符或 UNC 或根路径）
    if (p.size() >= 2 && p[1] == ':') return p;
    if (p[0] == '\\' || p[0] == '/') return p;
    // 相对路径：优先工作目录（存在时），否则用 exe 目录
    if (file_exists(p)) return p;
    return exe_dir() + "\\" + p;
}

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::optional<unsigned> parse_color(const std::string& s) {
    std::string t = trim(s);
    if (t.empty() || t[0] != '#') return std::nullopt;
    // #RGB / #RGBA / #RRGGBB / #AARRGGBB
    size_t n = t.size() - 1;
    if (n == 3 || n == 4) { // 每通道 1 位
        unsigned v = 0;
        for (size_t i = 0; i < n; i++) {
            int h = hex_val(t[1 + i]);
            if (h < 0) return std::nullopt;
            v = (v << 4) | (unsigned)h;
        }
        unsigned r = (v >> 8) & 0xF, g = (v >> 4) & 0xF, b = v & 0xF;
        unsigned a = (n == 4) ? ((v >> 12) & 0xF) : 0xF;
        auto scale = [](unsigned x) { return (x << 4) | x; };
        return (a << 24) | (scale(r) << 16) | (scale(g) << 8) | scale(b);
    }
    if (n == 6 || n == 8) { // 每通道 2 位
        unsigned v = 0;
        for (size_t i = 0; i < n; i++) {
            int h = hex_val(t[1 + i]);
            if (h < 0) return std::nullopt;
            v = (v << 4) | (unsigned)h;
        }
        unsigned a = (n == 8) ? ((v >> 24) & 0xFF) : 0xFF;
        return a << 24 | (v & 0xFFFFFF);
    }
    return std::nullopt;
}

static std::map<std::string, std::string> parse_ini(const std::string& path) {
    std::map<std::string, std::string> kv;
    std::ifstream f(path, std::ios::binary);
    if (!f) return kv;
    std::string line, section;
    while (std::getline(f, line)) {
        // 去掉 \r 与注释
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string t = trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        if (t.front() == '[' && t.back() == ']') {
            section = trim(t.substr(1, t.size() - 2));
            continue;
        }
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(t.substr(0, eq));
        std::string val = trim(t.substr(eq + 1));
        // 去引号
        if (val.size() >= 2 && ((val.front() == '"' && val.back() == '"') ||
                                (val.front() == '\'' && val.back() == '\'')))
            val = val.substr(1, val.size() - 2);
        kv[section + "." + key] = val;
    }
    return kv;
}

static int get_int(const std::map<std::string, std::string>& kv, const std::string& k, int def) {
    auto it = kv.find(k);
    if (it == kv.end()) return def;
    return atoi(it->second.c_str());
}

static float get_float(const std::map<std::string, std::string>& kv, const std::string& k, float def) {
    auto it = kv.find(k);
    if (it == kv.end()) return def;
    return (float)atof(it->second.c_str());
}

static std::string get_str(const std::map<std::string, std::string>& kv, const std::string& k, const std::string& def) {
    auto it = kv.find(k);
    if (it == kv.end()) return def;
    return it->second;
}

static bool get_bool(const std::map<std::string, std::string>& kv, const std::string& k, bool def) {
    auto it = kv.find(k);
    if (it == kv.end()) return def;
    std::string v = it->second;
    std::transform(v.begin(), v.end(), v.begin(), ::tolower);
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

Config Config::load(const std::string& path) {
    Config c;
    c.set_path(path);
    auto kv = parse_ini(path);

    c.sample_rate        = get_int(kv, "audio.sample_rate", c.sample_rate);
    c.device_id          = get_str(kv, "audio.device_id", c.device_id);
    c.input_boost_db     = get_float(kv, "audio.input_boost_db", c.input_boost_db);

    c.vad_threshold_db   = get_float(kv, "vad.threshold_db", c.vad_threshold_db);
    c.vad_margin_db      = get_float(kv, "vad.margin_db", c.vad_margin_db);
    c.min_speech_ms      = get_int(kv, "vad.min_speech_ms", c.min_speech_ms);
    c.silence_ms         = get_int(kv, "vad.silence_ms", c.silence_ms);

    c.model_path         = get_str(kv, "asr.model_path", c.model_path);
    c.mmproj_path        = get_str(kv, "asr.mmproj_path", c.mmproj_path);
    c.n_threads          = get_int(kv, "asr.n_threads", c.n_threads);
    c.gpu_layers         = get_int(kv, "asr.gpu_layers", c.gpu_layers);
    c.n_batch            = get_int(kv, "asr.n_batch", c.n_batch);
    c.chunk_ms           = get_int(kv, "asr.chunk_ms", c.chunk_ms);
    c.hop_ms             = get_int(kv, "asr.hop_ms", c.hop_ms);
    c.max_new_tokens     = get_int(kv, "asr.max_new_tokens", c.max_new_tokens);
    c.prompt             = get_str(kv, "asr.prompt", c.prompt);

    c.font_family        = get_str(kv, "ui.font_family", c.font_family);
    c.font_size          = get_float(kv, "ui.font_size", c.font_size);
    c.font_color         = get_str(kv, "ui.font_color", c.font_color);
    c.bg_color           = get_str(kv, "ui.bg_color", c.bg_color);
    c.stroke_enabled     = get_bool(kv, "ui.stroke_enabled", c.stroke_enabled);
    c.stroke_color       = get_str(kv, "ui.stroke_color", c.stroke_color);
    c.stroke_width       = std::max(0, std::min(3, get_int(kv, "ui.stroke_width", c.stroke_width))); // 0-3，0=关闭
    c.window_w           = get_int(kv, "ui.window_w", c.window_w);
    c.window_h           = get_int(kv, "ui.window_h", c.window_h);
    c.pos_x              = get_int(kv, "ui.pos_x", c.pos_x);
    c.pos_y              = get_int(kv, "ui.pos_y", c.pos_y);
    // 旧版兼容：<=200 视为屏幕百分比（旧默认 50/85），迁移为像素中心坐标
    if (c.pos_x <= 200) c.pos_x = GetSystemMetrics(SM_CXSCREEN) * c.pos_x / 100;
    if (c.pos_y <= 200) c.pos_y = GetSystemMetrics(SM_CYSCREEN) * c.pos_y / 100;
    c.max_lines          = get_int(kv, "ui.max_lines", c.max_lines);
    c.always_on_top      = get_bool(kv, "ui.always_on_top", c.always_on_top);
    c.click_through      = get_bool(kv, "ui.click_through", c.click_through);
    c.show_status        = get_bool(kv, "ui.show_status", c.show_status);
    c.fade_in_ms         = get_int(kv, "ui.fade_in_ms", c.fade_in_ms);
    c.fade_out_ms        = get_int(kv, "ui.fade_out_ms", c.fade_out_ms);
    c.fps                = get_int(kv, "ui.fps", c.fps);

    c.write_srt          = get_bool(kv, "output.write_srt", c.write_srt);
    c.srt_path           = get_str(kv, "output.srt_path", c.srt_path);
    c.write_text         = get_bool(kv, "output.write_text", c.write_text);
    c.text_path          = get_str(kv, "output.text_path", c.text_path);

    c.mic_enabled        = get_bool(kv, "tracks.mic_enabled", c.mic_enabled);
    c.pc_enabled         = get_bool(kv, "tracks.pc_enabled", c.pc_enabled);

    c.log_level          = get_int(kv, "log.log_level", c.log_level);

    // 模型路径：按 model_size 解析默认路径（用户自定义路径时保留）
    if (c.model_size != "small") c.model_size = "large";
    const bool is_large_default = (c.model_path.find("Qwen3-ASR-1.7B") != std::string::npos) ||
                                  (c.model_path == "model/Qwen3-ASR-1.7B-Q8_0.gguf");
    const bool is_small_default = (c.model_path.find("Qwen3-ASR-0.6B") != std::string::npos);
    if (c.model_size == "small" && (is_large_default || c.model_path.empty())) {
        c.model_path  = "model/Qwen3-ASR-0.6B-Q8_0.gguf";
        c.mmproj_path = "model/mmproj-Qwen3-ASR-0.6B-bf16.gguf";
    } else if (c.model_size == "large" && (is_small_default || c.model_path.empty())) {
        c.model_path  = "model/Qwen3-ASR-1.7B-Q8_0.gguf";
        c.mmproj_path = "model/mmproj-Qwen3-ASR-1.7B-bf16.gguf";
    }
    return c;
}

void Config::save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return;
    auto w = [&](const std::string& s) { f << s << "\n"; };
    w("; LiveSub 配置文件");
    w("");
    w("[audio]");
    w("sample_rate = " + std::to_string(sample_rate));
    w("device_id = " + device_id);
    w("input_boost_db = " + std::to_string(input_boost_db));
    w("");
    w("[vad]");
    w("threshold_db = " + std::to_string(vad_threshold_db));
    w("margin_db = " + std::to_string(vad_margin_db));
    w("min_speech_ms = " + std::to_string(min_speech_ms));
    w("silence_ms = " + std::to_string(silence_ms));
    w("");
    w("[asr]");
    w("model_size = " + model_size);
    w("model_path = " + model_path);
    w("mmproj_path = " + mmproj_path);
    w("n_threads = " + std::to_string(n_threads));
    w("gpu_layers = " + std::to_string(gpu_layers));
    w("n_batch = " + std::to_string(n_batch));
    w("chunk_ms = " + std::to_string(chunk_ms));
    w("hop_ms = " + std::to_string(hop_ms));
    w("max_new_tokens = " + std::to_string(max_new_tokens));
    w("prompt = " + prompt);
    w("");
    w("[ui]");
    w("font_family = " + font_family);
    w("font_size = " + std::to_string(font_size));
    w("font_color = " + font_color);
    w("bg_color = " + bg_color);
    w("stroke_enabled = " + std::string(stroke_enabled ? "true" : "false"));
    w("stroke_color = " + stroke_color);
    w("stroke_width = " + std::to_string(stroke_width));
    w("window_w = " + std::to_string(window_w));
    w("window_h = " + std::to_string(window_h));
    w("pos_x = " + std::to_string(pos_x));
    w("pos_y = " + std::to_string(pos_y));
    w("max_lines = " + std::to_string(max_lines));
    w("always_on_top = " + std::string(always_on_top ? "true" : "false"));
    w("click_through = " + std::string(click_through ? "true" : "false"));
    w("show_status = " + std::string(show_status ? "true" : "false"));
    w("fade_in_ms = " + std::to_string(fade_in_ms));
    w("fade_out_ms = " + std::to_string(fade_out_ms));
    w("fps = " + std::to_string(fps));
    w("");
    w("[output]");
    w("write_srt = " + std::string(write_srt ? "true" : "false"));
    w("srt_path = " + srt_path);
    w("write_text = " + std::string(write_text ? "true" : "false"));
    w("text_path = " + text_path);
    w("");
    w("[tracks]");
    w("mic_enabled = " + std::string(mic_enabled ? "true" : "false"));
    w("pc_enabled = " + std::string(pc_enabled ? "true" : "false"));
    w("");
    w("[log]");
    w("log_level = " + std::to_string(log_level));
}
