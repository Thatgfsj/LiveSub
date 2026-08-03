#include "app.h"

#include <cstdio>
#include <cmath>
#include <algorithm>
#include <thread>

#include "audio/wav_reader.h"

// 全局单实例指针（前台窗口事件回调使用）
static App* g_app = nullptr;

App::~App() {
    shutdown();
    if (log_file_) { fclose(log_file_); log_file_ = nullptr; }
}

// 前台窗口变化事件：全屏应用出现 → 立即把字幕窗口提到置顶层顶部
// （事件驱动，非轮询：只在全屏瞬间动作，平时零开销）
void CALLBACK App::on_foreground_event(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    App* self = g_app;
    if (!self || !self->window_.ok() || !self->cfg_.always_on_top) return;
    const HWND fg = GetForegroundWindow();
    if (!fg || fg == self->window_.hwnd()) return;
    RECT r;
    if (!GetWindowRect(fg, &r)) return;
    // 前台窗口覆盖其所在显示器的完整区域 → 视为全屏
    HMONITOR mon = MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (!GetMonitorInfo(mon, &mi)) return;
    const bool fullscreen =
        r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top &&
        r.right >= mi.rcMonitor.right && r.bottom >= mi.rcMonitor.bottom;
    if (fullscreen) {
        SetWindowPos(self->window_.hwnd(), HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void App::logf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    if (log_file_) {
        va_start(args, fmt);
        vfprintf(log_file_, fmt, args);
        fflush(log_file_);
    }
    va_end(args);
}

void App::update_tray(TrayIcon::State s, const std::string& tip) {
    if (!tray_ready_) return;
    tray_.set_state(s, utf8_to_wide(tip));
}

// ---------------------------------------------------------------------------
// 管线启停
// ---------------------------------------------------------------------------
// 唯一共享字幕窗口（两条管线共用同一展示框，整窗显示当前识别的字幕）。
// 位置按 ui.pos_x/pos_y 像素中心，并 clamp 到屏幕内（字幕完整可见）。
bool App::ensure_window() {
    if (window_.ok()) return true;

    SubtitleWindow::Style st;
    st.font_family   = utf8_to_wide(cfg_.font_family);
    st.font_size     = cfg_.font_size;
    st.min_font_size = cfg_.min_font_size;
    st.font_color    = parse_color(cfg_.font_color).value_or(0xFFFFFFFF);
    st.bg_color      = parse_color(cfg_.bg_color).value_or(0xC0000000);
    st.window_alpha  = (float)cfg_.window_alpha / 100.0f;
    st.window_w      = cfg_.window_w;
    st.window_h      = cfg_.window_h;
    st.max_lines     = cfg_.max_lines;
    st.always_on_top = cfg_.always_on_top;
    st.click_through = cfg_.click_through;
    st.show_status   = cfg_.show_status;
    st.fade_in_ms    = cfg_.fade_in_ms;
    st.fade_out_ms   = cfg_.fade_out_ms;
    st.fps           = cfg_.fps;
    st.stroke_enabled = cfg_.stroke_enabled;
    st.stroke_color  = parse_color(cfg_.stroke_color).value_or(0xFF000000);
    st.stroke_width  = cfg_.stroke_width;
    // 位置：像素中心坐标（pos_x/pos_y 为窗口中心点）。
    // 中心 clamp 到屏幕内（字幕文本约 140px 高，中心留 [80, sh-80] 保证完整可见）：
    // 1080p 屏默认 Y=1250 自动落到 1000，字幕仍在屏幕底部可见；2K 屏设置的 1250 不受影响
    const int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    const int cx = std::max(80, std::min(cfg_.pos_x, sw - 80));
    const int cy = std::max(80, std::min(cfg_.pos_y, sh - 80));
    st.window_x = std::max(0, std::min(cx - st.window_w / 2, sw - st.window_w));
    st.window_y = std::max(0, cy - st.window_h / 2);
    std::wstring err;
    if (!window_.create(st, &err)) {
        logf("[app] 字幕窗口创建失败: %ls\n", err.c_str());
        return false;
    }
    return true;
}

bool App::start_pipeline(AsrPipeline& p, bool is_mic, bool start_capture) {
    std::lock_guard<std::mutex> plk(pipeline_mtx_);
    if (p.enabled.load()) return true;

    // 共享窗口：只创建一次，两条管线共用
    if (!ensure_window()) return false;
    p.window = &window_;

    // 队列 / VAD / 重采样
    p.resampler = new Resampler(cfg_.sample_rate, asr_.sample_rate());
    p.queue = new AudioQueue(asr_.sample_rate(), cfg_.chunk_ms * 3, cfg_.chunk_ms, cfg_.hop_ms);
    Vad::Params vp;
    vp.sample_rate   = asr_.sample_rate();
    vp.threshold_db  = cfg_.vad_threshold_db;
    vp.margin_db     = cfg_.vad_margin_db;
    vp.min_speech_ms = cfg_.min_speech_ms;
    vp.silence_ms    = cfg_.silence_ms;
    p.vad = new Vad(vp);
    p.vad->on_speech_start = [this, &p](int64_t start_ms) {
        p.speaking = true;
        // 段起点用 VAD 报告的语音真实起点（毫秒→样本序号），
        // 不能用回调时刻的 total_samples——那会晚约 min_speech_ms(250ms)，
        // 把开头的 1-2 个字切掉（"第一个字识别不到"）
        if (p.queue) p.seg_start = (size_t)(start_ms * asr_.sample_rate() / 1000);
        // 新段清空上一句：ASR 线程处理（见 process_pipeline），
        // 避免"定稿句+新句"混存导致行数/视觉混乱
        p.clear_merger = true;
        // 不再显示"识别中…"：频繁说话时它会反复闪现（蹦迪感），
        // 字幕本身 1 秒内就会上屏，无需状态文字
    };
    p.vad->on_speech_end = [this, &p](int64_t end_ms) {
        p.speaking = false;
        if (p.queue) {
            // 段尾同样用 VAD 报告的时间（语音结束+静音），与队列累计基准一致
            p.seg_end = (size_t)(end_ms * asr_.sample_rate() / 1000);
            // 快照段边界：避免新段 speech_start 覆盖 seg_start，
            // 导致 finalize 取段失败（定稿/语音输入丢失）
            p.finalize_seg_start = p.seg_start.load();
            p.finalize_seg_end   = p.seg_end.load();
        }
        p.finalize_pending = true;
        if (p.window && &p == &mic_) p.window->set_status("");
    };
    p.resample_buf.resize((size_t)asr_.sample_rate() * 2);
    p.win_buf.reserve((size_t)asr_.sample_rate() * 32);
    p.merger.set_max_lines(cfg_.max_lines);

    // 输出（按设置：写文本文件 / 写 SRT）
    TextOutput::Config oc;
    oc.write_text = cfg_.write_text;
    oc.text_path  = cfg_.text_path;
    oc.write_srt  = cfg_.write_srt;
    oc.srt_path   = cfg_.srt_path;
    p.output.configure(oc);

    if (start_capture) {
        // 采集（麦克风 / 电脑声音 loopback）
        WasapiCapture::Config cc;
        cc.device_id  = cfg_.device_id;
        cc.boost_db   = cfg_.input_boost_db;
        cc.loopback   = !is_mic;
        p.capture.on_audio = [this, &p](const float* pcm, size_t n, int64_t t_ms) {
            on_audio(p, pcm, n, t_ms);
        };
        std::string cerr;
        if (!p.capture.start(cc, &cerr)) {
            logf("[%s] 采集失败: %s\n", p.name.c_str(), cerr.c_str());
            stop_pipeline(p);
            return false;
        }
    
    }

    p.queue->start();
    p.enabled = true;
    logf("[%s] 字幕已开启（%s）\n", p.name.c_str(), is_mic ? "麦克风" : "电脑声音");
    return true;
}

void App::stop_pipeline(AsrPipeline& p) {
    std::lock_guard<std::mutex> plk(pipeline_mtx_);
    p.enabled = false;
    if (p.queue) p.queue->stop();
    p.capture.stop(); // 先停采集线程（VAD 回调随之停止）
    // 采集线程已停（capture.stop 内部 join），此时置空 window 指针安全
    p.window = nullptr;
    if (p.vad) { delete p.vad; p.vad = nullptr; }
    if (p.queue) { delete p.queue; p.queue = nullptr; }
    if (p.resampler) { delete p.resampler; p.resampler = nullptr; }
    p.merger.clear();
    p.output.clear();
    p.speaking = false;
    p.finalize_pending = false;
    // 两条管线都停 → 清空共享窗口内容（窗口保留，内容空了会淡出）
    if (!mic_.enabled.load() && !pc_.enabled.load()) {
        window_.set_text("");
        window_.set_status("");
    }
    logf("[%s] 字幕已关闭\n", p.name.c_str());
}

void App::toggle_pipeline(AsrPipeline& p, bool enable) {
    if (enable) {
        const bool is_mic = (&p == &mic_);
        start_pipeline(p, is_mic);
        // 回写配置并保存：设置窗口与托盘状态保持一致（下次打开设置显示同步状态）
        if (is_mic) { cfg_.mic_enabled = p.enabled.load(); tray_.set_mic_enabled(p.enabled.load()); }
        else        { cfg_.pc_enabled  = p.enabled.load(); tray_.set_pc_enabled(p.enabled.load()); }
    } else {
        stop_pipeline(p);
        if (&p == &mic_) { cfg_.mic_enabled = false; tray_.set_mic_enabled(false); }
        else             { cfg_.pc_enabled  = false; tray_.set_pc_enabled(false); }
    }
    cfg_.save(cfg_.path());
}

// ---------------------------------------------------------------------------
// 初始化
// ---------------------------------------------------------------------------
bool App::init(const std::string& config_path, bool enable_capture) {
    cfg_ = Config::load(config_path);
    cfg_.set_path(config_path);

    // 托盘
    tray_.on_open_settings = [this]() { open_settings(); };
    tray_.on_toggle_window = [this]() {
        if (window_.hwnd()) {
            const bool vis = IsWindowVisible(window_.hwnd());
            ShowWindow(window_.hwnd(), vis ? SW_HIDE : SW_SHOWNOACTIVATE);
        }
    };
    tray_.on_quit = [this]() { PostQuitMessage(0); };
    tray_.on_toggle_mic = [this]() { toggle_pipeline(mic_, !mic_.enabled.load()); };
    tray_.on_toggle_pc  = [this]() { toggle_pipeline(pc_, !pc_.enabled.load()); };
    tray_.on_toggle_voice = [this]() {
        if (voice_input_.enabled()) {
            voice_input_.set_enabled(false);
            tray_.set_voice_input(false);
            if (window_.ok()) window_.set_status("语音输入已关闭");
            logf("[app] 语音输入已关闭\n");
        } else {
            voice_input_.set_enabled(true);
            tray_.set_voice_input(true);
            if (window_.ok()) window_.set_status("语音输入已开启：说话将输入到当前窗口");
            logf("[app] 语音输入已开启\n");
        }
    };
    tray_.on_toggle_record = [this]() {
        if (mic_.output.recording()) {
            mic_.output.stop_recording();
            tray_.set_recording(false);
            update_tray(TrayIcon::State::Ready, "LiveSub 记录已结束，文件在桌面");
            if (window_.ok()) window_.set_status("记录已结束，文件在桌面");
        } else {
            std::string path;
            if (mic_.output.start_recording(&path)) {
                tray_.set_recording(true);
                update_tray(TrayIcon::State::Ready, "LiveSub 正在记录讲话稿…");
                if (window_.ok()) window_.set_status("正在记录讲话稿…");
                logf("[app] 开始记录讲话稿: %s\n", path.c_str());
            } else {
                if (window_.ok()) window_.set_status("记录启动失败（无法创建桌面文件）");
            }
        }
    };
    tray_ready_ = tray_.create(L"LiveSub 字幕");
    update_tray(TrayIcon::State::Loading, "LiveSub 加载中…");

    // 日志
    {
        const std::string log_path = resolve_path("livesub.log");
        FILE* f = fopen(log_path.c_str(), "rb");
        if (f) {
            _fseeki64(f, 0, SEEK_END);
            const long long sz = _ftelli64(f);
            fclose(f);
            if (sz > 2 * 1024 * 1024) remove(log_path.c_str());
        }
        log_file_ = fopen(log_path.c_str(), "ab");
    }
    if (log_file_) logf("\n===== LiveSub 启动 =====\n");
    logf("[app] 加载配置: %s\n", config_path.c_str());

    mic_.name = "麦克风";
    pc_.name = "电脑声音";
    running_model_size_ = cfg_.model_size; // 记录当前运行模型

    // 模型引擎（共享单实例）
    AsrEngine::Params ap;
    ap.model_path   = resolve_path(cfg_.model_path);
    ap.mmproj_path  = resolve_path(cfg_.mmproj_path);
    ap.n_threads    = cfg_.n_threads;
    ap.gpu_layers   = cfg_.gpu_layers;
    ap.n_batch      = cfg_.n_batch;
    ap.max_new_tokens = cfg_.max_new_tokens;
    ap.prompt       = cfg_.prompt;
    ap.verbosity    = cfg_.log_level;
    std::string aerr;
    if (!asr_.init(ap, &aerr)) {
        logf("[app] ASR 引擎初始化失败: %s\n", aerr.c_str());
        update_tray(TrayIcon::State::Error, "LiveSub 出错: " + aerr);
        return false;
    }

    // GPU 预热
    {
        std::vector<float> silence((size_t)asr_.sample_rate(), 0.0f);
        AsrEngine::Result r = asr_.transcribe(silence.data(), silence.size());
        logf("[app] 预热完成 %s\n", r.ok ? "OK" : "失败");
    }

    // 启动管线（按配置）
    if (enable_capture) {
        if (cfg_.mic_enabled) {
            start_pipeline(mic_, true);
            tray_.set_mic_enabled(true);
        }
        if (cfg_.pc_enabled) {
            start_pipeline(pc_, false);
            tray_.set_pc_enabled(true);
        }
    }

    // ASR 线程
    asr_running_ = true;
    asr_thread_ = std::thread(&App::asr_loop, this);

    // 安装前台窗口变化钩子（全屏检测，根本方案）
    g_app = this;
    win_event_hook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                                      nullptr, on_foreground_event, 0, 0,
                                      WINEVENT_OUTOFCONTEXT);

    update_tray(TrayIcon::State::Ready, "LiveSub 就绪（双击设置）");
    logf("[app] 启动完成\n");
    return true;
}

// ---------------------------------------------------------------------------
// 采集回调
// ---------------------------------------------------------------------------
void App::on_audio(AsrPipeline& p, const float* pcm, size_t n, int64_t t_ms) {
    if (!p.resampler || !p.queue) return;
    const size_t n16 = p.resampler->process(pcm, n, p.resample_buf.data(), p.resample_buf.size());
    if (n16 == 0) return;
    if (p.vad) p.vad->process(p.resample_buf.data(), n16, t_ms);
    p.queue->push(p.resample_buf.data(), n16);
}

// ---------------------------------------------------------------------------
// ASR 线程：单引擎串行处理两条管线
// ---------------------------------------------------------------------------
bool App::process_pipeline(AsrPipeline& p) {
    // 与 stop_pipeline/start_pipeline 互斥：防止 ASR 线程使用 queue/vad 时被并发 delete
    std::lock_guard<std::mutex> plk(pipeline_mtx_);
    // 新段开始：清空上一句（每段只显示当前句，行数恒定）
    if (p.clear_merger.exchange(false)) {
        p.merger.clear();
    }
    const int64_t t_now = now_ms();
    const bool finalize = p.finalize_pending.exchange(false);
    if (!p.enabled.load() || !p.queue) {
        if (finalize) p.finalize_pending = true; // 恢复标志，稍后处理
        return false;
    }
    if (!p.speaking.load() && !finalize) {
        last_asr_heartbeat_ms_ = t_now;
        return false;
    }

    // 数据增量不足一个 hop → 跳过（超时醒来防重复）
    const size_t total_now = p.queue->total_samples();
    const size_t hop_samples = (size_t)asr_.sample_rate() * cfg_.hop_ms / 1000;
    if (!finalize && total_now - p.last_processed.load() < hop_samples) {
        last_asr_heartbeat_ms_ = t_now;
        return false;
    }
    p.last_processed = total_now;

    // 新段首窗最短 1.0s（对齐 whisper_streaming 默认 min-chunk-size=1.0s）
    if (!finalize && total_now - p.seg_start.load() < (size_t)asr_.sample_rate()) {
        last_asr_heartbeat_ms_ = t_now;
        return false;
    }

    // 取窗口：finalize 用段边界快照（不被新段覆盖）；
    // 窗口上限统一 8s——定稿与最后一次 interim 用同一窗口 → 文本无缝不蹦字
    const size_t seg_now = finalize ? p.finalize_seg_start.load() : p.seg_start.load();
    const size_t seg_end = finalize ? p.finalize_seg_end.load() : 0;
    const size_t max_len = (size_t)asr_.sample_rate() * 8;
    const size_t n = p.queue->take_segment(seg_now, seg_end, max_len, p.win_buf);
    if (n == 0) return false;

    // 活跃帧比例（防噪音误触发）
    if (!finalize) {
        const float th = p.vad ? p.vad->current_threshold_db() - 6.0f : -60.0f;
        const size_t frame = (size_t)asr_.sample_rate() * 20 / 1000;
        int active = 0, frames = 0;
        for (size_t i = 0; i + frame <= n; i += frame) {
            double sum = 0.0;
            for (size_t j = 0; j < frame; j++) {
                sum += (double)p.win_buf[i + j] * p.win_buf[i + j];
            }
            const float db = 20.0f * (float)std::log10(std::sqrt(sum / frame) + 1e-12);
            if (db > th) active++;
            frames++;
        }
        if (frames > 0 && active * 100 < frames * 15) {
            last_asr_heartbeat_ms_ = t_now;
            return false;
        }
    }

    p.merger.prune(now_ms());

    // 共享单引擎：互斥串行识别
    const int64_t t0 = now_ms();
    AsrEngine::Result r;
    {
        std::lock_guard<std::mutex> lk(asr_mtx_);
        try {
            r = asr_.transcribe(p.win_buf.data(), n);
        } catch (const std::exception& e) {
            logf("[%s] 引擎异常: %s\n", p.name.c_str(), e.what());
            if (&p == &mic_) p.window->set_status("识别引擎异常: " + std::string(e.what()));
            last_asr_heartbeat_ms_ = now_ms();
            return false;
        }
    }
    const int64_t cost = now_ms() - t0;
    last_asr_heartbeat_ms_ = now_ms();

    if (r.ok) {
        const std::string full = p.merger.update(r.text, finalize, now_ms());
        if (!r.text.empty()) {
            // 单一展示框整窗显示：哪条管线在识别就显示谁的字幕
            p.window->set_text(full, p.merger.confirmed_offset());
            // 输出：interim 更新文本文件（OBS 轮询实时字幕），定稿句写入/追加
            p.output.update(full, finalize ? r.text : std::string());
            if (finalize) {
                p.window->set_status("");
                // 讲话稿记录（托盘"开始记录"开启时）追加到桌面文件
                p.output.append_record(r.text);
                // 语音输入：只对麦克风轨定稿句输入
                if (&p == &mic_ && voice_input_.enabled()) {
                    voice_input_.commit_text(r.text + " ");
                }
            }
        } else if (finalize) {
            // 定稿但无文本（噪音/气声）：不弹状态文字（避免屏幕蹦字），仅记日志
        }
        if (cfg_.log_level >= 1) {
            logf("[%s] %s | total=%lldms%s\n", p.name.c_str(),
                 r.text.empty() ? "(空)" : r.text.c_str(),
                 (long long)cost, finalize ? " [FINAL]" : "");
        }
    } else {
        if (&p == &mic_) p.window->set_status("识别错误: " + asr_.last_error());
        if (cfg_.log_level >= 1) {
            logf("[%s] 识别失败: %s\n", p.name.c_str(), asr_.last_error().c_str());
        }
    }
    return true;
}

void App::asr_loop() {
    while (asr_running_) {
        bool worked = false;
        if (mic_.enabled.load() || mic_.finalize_pending.load()) worked |= process_pipeline(mic_);
        if (pc_.enabled.load()  || pc_.finalize_pending.load())  worked |= process_pipeline(pc_);
        if (!worked) {
            // 等新数据（200ms 超时，供定稿唤醒）
            if (mic_.queue) mic_.queue->wait_for_window();
            if (pc_.queue)  pc_.queue->wait_for_window();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

// ---------------------------------------------------------------------------
// wav 测试（喂给麦克风管线）
// ---------------------------------------------------------------------------
bool App::run_wav_test(const std::string& wav_path) {
    std::vector<float> pcm;
    int wav_rate = 0;
    if (!read_wav_mono(wav_path, pcm, wav_rate)) {
        logf("[app] 无法读取 wav: %s\n", wav_path.c_str());
        return false;
    }
    std::vector<float> pcm16;
    const int target = asr_.sample_rate();
    if (wav_rate != target) {
        Resampler rs(wav_rate, target);
        pcm16.resize(pcm.size() * target / wav_rate + 64);
        pcm16.resize(rs.process(pcm.data(), pcm.size(), pcm16.data(), pcm16.size()));
    } else {
        pcm16 = std::move(pcm);
    }
    logf("[app] wav 测试: %s  %dHz  %.1fs\n", wav_path.c_str(), wav_rate,
         (double)pcm16.size() / target);

    // wav 测试：确保麦克风管线就绪（不启动真实采集）
    if (!mic_.enabled.load()) {
        start_pipeline(mic_, true, false);
    }

    const size_t block = (size_t)target * 20 / 1000;
    size_t off = 0;
    while (off < pcm16.size() && asr_running_) {
        const size_t n = std::min(block, pcm16.size() - off);
        if (mic_.vad) mic_.vad->process(pcm16.data() + off, n, 0);
        if (mic_.queue) mic_.queue->push(pcm16.data() + off, n);
        off += n;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (mic_.vad) mic_.vad->force_end(now_ms());
    const int64_t t_end = now_ms() + 6000;
    while (now_ms() < t_end && asr_running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    logf("[app] wav 测试结束\n");
    return true;
}

// ---------------------------------------------------------------------------
// 设置 / 运行 / 退出
// ---------------------------------------------------------------------------
void App::apply_config() {
    cfg_.save(cfg_.path());

    // 模型切换检测：切换到没有对应模型文件的大小 → 提示并打开下载器
    // 缺失时跳过下面的重启提示（模型都没下载，重启没有意义）
    const bool model_missing = check_model_files();

    // 模型大小切换：需要重启才生效 → 提示，确认则自动重启，取消则不重启
    if (cfg_.model_size != running_model_size_ && !model_missing) {
        const wchar_t* which = (cfg_.model_size == "small") ? L"小模型（0.6B）" : L"大模型（1.7B）";
        const std::wstring msg = std::wstring(L"模型已切换为") + which +
            L"，重启应用后生效。\n\n是否立即重启？";
        const int ret = MessageBoxW(nullptr, msg.c_str(), L"LiveSub 模型切换",
                                    MB_YESNO | MB_ICONQUESTION);
        if (ret == IDYES) {
            // 自动重启：先启动新实例，再退出当前进程
            wchar_t exe[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, exe, MAX_PATH);
            wchar_t dir[MAX_PATH] = {};
            wcsncpy(dir, exe, MAX_PATH);
            wchar_t* slash = wcsrchr(dir, L'\\');
            if (slash) *slash = L'\0';
            ShellExecuteW(nullptr, L"open", exe, nullptr, dir, SW_SHOWNORMAL);
            PostQuitMessage(0);
        }
        // 取消：不重启（config 已保存，下次启动生效）
    }

    // 共享窗口需销毁重建才能应用位置/样式改动
    // （ensure_window 只做懒创建，窗口存在时不会重建 → 新配置不生效）
    window_.destroy();

    // 按设置里的开关状态对齐两条管线（设置窗口勾选变化 → 立即启停）
    // 并同步托盘状态；已开启的管线重启以应用其余配置
    const bool mic_target = cfg_.mic_enabled, pc_target = cfg_.pc_enabled;
    if (mic_.enabled.load() != mic_target) {
        if (mic_target) start_pipeline(mic_, true);
        else            stop_pipeline(mic_);
        tray_.set_mic_enabled(mic_target);
    } else if (mic_target) {
        stop_pipeline(mic_);
        start_pipeline(mic_, true);
    }
    if (pc_.enabled.load() != pc_target) {
        if (pc_target) start_pipeline(pc_, false);
        else           stop_pipeline(pc_);
        tray_.set_pc_enabled(pc_target);
    } else if (pc_target) {
        stop_pipeline(pc_);
        start_pipeline(pc_, false);
    }
    logf("[app] 设置已应用\n");
}

// 切换模型大小后，检查对应模型文件是否存在/完整；
// 缺失 → 弹窗提示并可直接打开 model-dl.exe（带 --auto 直接下载对应大小）
bool App::check_model_files() {
    const std::string mp = resolve_path("model");
    std::string main_f, proj_f;
    if (cfg_.model_size == "small") {
        main_f = mp + "\\Qwen3-ASR-0.6B-Q8_0.gguf";
        proj_f = mp + "\\mmproj-Qwen3-ASR-0.6B-bf16.gguf";
    } else {
        main_f = mp + "\\Qwen3-ASR-1.7B-Q8_0.gguf";
        proj_f = mp + "\\mmproj-Qwen3-ASR-1.7B-bf16.gguf";
    }
    // 文件大小检查（与启动检测一致：主模型 1GB/500MB，编码器 100MB）
    auto size_ok = [](const std::string& p, long long min_bytes) {
        FILE* f = fopen(p.c_str(), "rb");
        if (!f) return false;
        _fseeki64(f, 0, SEEK_END);
        const long long sz = _ftelli64(f);
        fclose(f);
        return sz > min_bytes;
    };
    const bool main_ok = size_ok(main_f, cfg_.model_size == "small" ? 500000000LL : 1000000000LL);
    const bool proj_ok = size_ok(proj_f, 100000000LL);
    if (main_ok && proj_ok) return false;

    const std::string which = (cfg_.model_size == "small") ? "小模型（0.6B）" : "大模型（1.7B）";
    std::string miss;
    if (!main_ok) miss += "主模型文件未找到或不完整:\n  " + main_f;
    if (!proj_ok) miss += std::string(miss.empty() ? "" : "\n") + "音频编码器未找到或不完整:\n  " + proj_f;
    const std::wstring msg =
        utf8_to_wide(which + "的" + miss + "\n\n是否现在打开模型下载器下载？");
    const int ret = MessageBoxW(nullptr, msg.c_str(), L"LiveSub 模型缺失",
                                MB_YESNO | MB_ICONWARNING);
    if (ret == IDYES) {
        const wchar_t* arg = (cfg_.model_size == "small") ? L"--auto --small" : L"--auto --large";
        ShellExecuteW(nullptr, L"open", L"model-dl.exe", arg, nullptr, SW_SHOWNORMAL);
    }
    return true; // 目标模型缺失
}

void App::open_settings() {
    SettingsWindow win(cfg_, [this]() { apply_config(); }, []() {});
    win.run();
}

void App::run() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void App::shutdown() {
    if (shutdown_done_.exchange(true)) return;
    // 卸载前台窗口事件钩子
    if (win_event_hook_) {
        UnhookWinEvent(win_event_hook_);
        win_event_hook_ = nullptr;
    }
    g_app = nullptr;
    // 先销毁托盘与字幕窗口：点"退出"后界面立刻消失，
    // 避免模型释放（asr_.free() 可能耗时数秒）期间看起来"没反应"，被误以为要点两次
    tray_.destroy();
    tray_ready_ = false;
    window_.destroy();
    asr_running_ = false;
    if (mic_.queue) mic_.queue->stop();
    if (pc_.queue)  pc_.queue->stop();
    if (asr_thread_.joinable()) asr_thread_.join();
    stop_pipeline(mic_);
    stop_pipeline(pc_);
    asr_.free();
}
