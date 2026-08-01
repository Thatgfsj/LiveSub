#include "app.h"

#include <cstdio>
#include <cmath>
#include <algorithm>
#include <thread>

#include "audio/wav_reader.h"

App::~App() {
    shutdown();
    if (log_file_) { fclose(log_file_); log_file_ = nullptr; }
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
// 唯一共享字幕窗口（两条管线共用一个展示框：mic 主轨上半区 / pc 第二轨下半区）。
// 位置按 ui.pos_x/pos_y 百分比，并 clamp 到屏幕内（保证第二轨字幕完整可见）。
bool App::ensure_window() {
    if (window_.ok()) return true;

    SubtitleWindow::Style st;
    st.font_family   = utf8_to_wide(cfg_.font_family);
    st.font_size     = cfg_.font_size;
    st.font_color    = parse_color(cfg_.font_color).value_or(0xFFFFFFFF);
    st.bg_color      = parse_color(cfg_.bg_color).value_or(0xC0000000);
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
    // 水平 clamp 到屏幕内（字幕横向完整可见）；垂直只保底 >=0，
    // 允许窗口底部略超出屏幕——字幕内容居中显示仍完整可见（默认中心 900 贴近底部）
    const int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    st.window_x = std::max(0, std::min(cfg_.pos_x - st.window_w / 2, sw - st.window_w));
    st.window_y = std::max(0, cfg_.pos_y - st.window_h / 2);
    std::wstring err;
    if (!window_.create(st, &err)) {
        logf("[app] 字幕窗口创建失败: %ls\n", err.c_str());
        return false;
    }
    return true;
}

bool App::start_pipeline(AsrPipeline& p, bool is_mic, bool start_capture) {
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
    p.vad->on_speech_start = [this, &p](int64_t) {
        p.speaking = true;
        if (p.queue) p.seg_start = p.queue->total_samples();
        // "识别中…"只归麦克风轨（PC 轨字幕直接上屏，不占用主轨状态位）
        if (&p == &mic_) p.window->set_status("识别中…");
    };
    p.vad->on_speech_end = [this, &p](int64_t) {
        p.speaking = false;
        if (p.queue) p.seg_end = p.queue->total_samples();
        p.finalize_pending = true;
        if (&p == &mic_) p.window->set_status("");
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
    p.enabled = false;
    p.window = nullptr;
    if (p.queue) p.queue->stop();
    p.capture.stop();
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
        window_.set_second_text("");
        window_.set_status("");
    }
    logf("[%s] 字幕已关闭\n", p.name.c_str());
}

void App::toggle_pipeline(AsrPipeline& p, bool enable) {
    if (enable) {
        const bool is_mic = (&p == &mic_);
        start_pipeline(p, is_mic);
        if (is_mic) tray_.set_mic_enabled(p.enabled.load());
        else tray_.set_pc_enabled(p.enabled.load());
    } else {
        stop_pipeline(p);
        if (&p == &mic_) tray_.set_mic_enabled(false);
        else tray_.set_pc_enabled(false);
    }
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

    // 新段首窗最短 1.5s
    const size_t seg_now = p.seg_start.load();
    if (!finalize && total_now - seg_now < (size_t)asr_.sample_rate() * 3 / 2) {
        last_asr_heartbeat_ms_ = t_now;
        return false;
    }

    // 取窗口（finalize 用固定段尾，上限 30s；部分结果 8s）
    const size_t seg_end = finalize ? p.seg_end.load() : 0;
    const size_t max_len = (size_t)asr_.sample_rate() * (finalize ? 30 : 8);
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
            // （不分割上下半区——两条字幕一般不会同时开）
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
            if (&p == &mic_) p.window->set_status("未识别到语音");
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
    check_model_files();

    // 共享窗口需销毁重建才能应用位置/样式改动
    // （ensure_window 只做懒创建，窗口存在时不会重建 → 新配置不生效）
    window_.destroy();

    // 重启两条管线（应用新配置）
    bool mic_on = mic_.enabled.load(), pc_on = pc_.enabled.load();
    if (mic_on) { stop_pipeline(mic_); start_pipeline(mic_, true); }
    if (pc_on)  { stop_pipeline(pc_);  start_pipeline(pc_, false); }
    logf("[app] 设置已应用\n");
}

// 切换模型大小后，检查对应模型文件是否存在/完整；
// 缺失 → 弹窗提示并可直接打开 model-dl.exe（带 --auto 直接下载对应大小）
void App::check_model_files() {
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
    if (main_ok && proj_ok) return;

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
