#include "app.h"

#include <cstdio>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <thread>

#include "audio/wav_reader.h"

using namespace std::chrono;

App::~App() {
    shutdown();
    if (log_file_) { fclose(log_file_); log_file_ = nullptr; }
}

void App::logf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    if (log_file_) {
        va_start(args, fmt); // 重新遍历
        vfprintf(log_file_, fmt, args);
        fflush(log_file_);
    }
    va_end(args);
}

void App::update_tray(TrayIcon::State s, const std::string& tip) {
    if (!tray_ready_) return;
    tray_.set_state(s, utf8_to_wide(tip));
}

bool App::init(const std::string& config_path, bool enable_capture) {
    cfg_ = Config::load(config_path);
    cfg_.set_path(config_path);

    // 托盘图标（右下角）：运行状态一目了然
    tray_.on_open_settings = [this]() { open_settings(); };
    tray_.on_toggle_window = [this]() {
        if (window_.hwnd()) {
            const bool vis = IsWindowVisible(window_.hwnd());
            ShowWindow(window_.hwnd(), vis ? SW_HIDE : SW_SHOWNOACTIVATE);
        }
    };
    tray_.on_quit = [this]() { PostQuitMessage(0); };
    tray_.on_toggle_voice = [this]() {
        if (voice_input_.enabled()) {
            voice_input_.set_enabled(false);
            tray_.set_voice_input(false);
            window_.set_status("语音输入已关闭");
            logf("[app] 语音输入已关闭\n");
        } else {
            voice_input_.set_enabled(true);
            tray_.set_voice_input(true);
            window_.set_status("语音输入已开启：说话将输入到当前窗口");
            update_tray(TrayIcon::State::Ready, "LiveSub 语音输入已开启");
            logf("[app] 语音输入已开启\n");
        }
    };
    tray_.on_toggle_record = [this]() {
        if (output_.recording()) {
            output_.stop_recording();
            tray_.set_recording(false);
            update_tray(TrayIcon::State::Ready, "LiveSub 记录已结束，文件在桌面");
            window_.set_status("记录已结束，文件在桌面");
        } else {
            std::string path;
            if (output_.start_recording(&path)) {
                tray_.set_recording(true);
                update_tray(TrayIcon::State::Ready, "LiveSub 正在记录讲话稿…");
                window_.set_status("正在记录讲话稿…");
                logf("[app] 开始记录讲话稿: %s\n", path.c_str());
            } else {
                window_.set_status("记录启动失败（无法创建桌面文件）");
            }
        }
    };
    tray_ready_ = tray_.create(L"LiveSub 字幕");
    update_tray(TrayIcon::State::Loading, "LiveSub 加载中…");

    // 日志文件（追加，便于排查）
    log_file_ = fopen((resolve_path("livesub.log")).c_str(), "ab");
    if (log_file_) {
        logf("\n===== LiveSub 启动 %s =====\n", "=====");
    }
    logf("[app] 加载配置: %s\n", config_path.c_str());

    // 1. 字幕窗口（主线程）
    SubtitleWindow::Style st;
    st.font_family   = utf8_to_wide(cfg_.font_family);
    st.font_size     = cfg_.font_size;
    st.font_color    = parse_color(cfg_.font_color).value_or(0xFFFFFFFF);
    st.bg_color      = parse_color(cfg_.bg_color).value_or(0xC0000000);
    st.window_w      = cfg_.window_w;
    st.window_h      = cfg_.window_h;
    // 百分比定位：pos_x/pos_y 为屏幕宽高的百分比（50=居中，越大越靠右/下）
    st.window_x      = GetSystemMetrics(SM_CXSCREEN) * cfg_.pos_x / 100 - cfg_.window_w / 2;
    st.window_y      = GetSystemMetrics(SM_CYSCREEN) * cfg_.pos_y / 100 - cfg_.window_h / 2;
    st.max_lines     = cfg_.max_lines;
    st.always_on_top = cfg_.always_on_top;
    st.click_through = cfg_.click_through;
    st.show_status   = cfg_.show_status;
    st.fade_in_ms    = cfg_.fade_in_ms;
    st.fade_out_ms   = cfg_.fade_out_ms;
    st.fps           = cfg_.fps;
    std::wstring err;
    if (!window_.create(st, &err)) {
        logf("[app] 字幕窗口创建失败: %ls\n", err.c_str());
        return false;
    }
    window_.set_status("加载模型中…");

    // 2. 输出
    TextOutput::Config oc;
    oc.write_text = cfg_.write_text;
    oc.text_path  = cfg_.text_path;
    oc.write_srt  = cfg_.write_srt;
    oc.srt_path   = cfg_.srt_path;
    output_.configure(oc);

    merger_.set_max_lines(cfg_.max_lines);

    // 3. ASR 引擎
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
        window_.set_status("模型加载失败: " + aerr);
        update_tray(TrayIcon::State::Error, "LiveSub 出错: " + aerr);
        return false;
    }

    // 4. 音频管线（采集回调运行在采集线程）
    resampler_ = new Resampler(cfg_.sample_rate, asr_.sample_rate());
    queue_ = new AudioQueue(asr_.sample_rate(), cfg_.chunk_ms * 3, cfg_.chunk_ms, cfg_.hop_ms);
    Vad::Params vp;
    vp.sample_rate   = asr_.sample_rate();
    vp.threshold_db  = cfg_.vad_threshold_db;
    vp.margin_db     = cfg_.vad_margin_db;
    vp.min_speech_ms = cfg_.min_speech_ms;
    vp.silence_ms    = cfg_.silence_ms;
    vad_ = new Vad(vp);
    vad_->on_speech_start = [this](int64_t) {
        speaking_ = true;
        // 记录语音段起点（识别窗口从这里开始，对齐句子开头）
        if (queue_) seg_start_total_ = queue_->total_samples();
        window_.set_status("识别中…");
        update_tray(TrayIcon::State::Listening, "LiveSub 识别中…");
    };
    vad_->on_speech_end = [this](int64_t) {
        speaking_ = false;
        // 记录段尾（固定定稿窗口终点，避免新段开始后 finalize 窗口漂移）
        if (queue_) seg_end_total_ = queue_->total_samples();
        finalize_pending_ = true; // 请求 ASR 线程定稿
        window_.set_status("");
        update_tray(TrayIcon::State::Ready, "LiveSub 就绪 · 请说话");
    };
    vad_->on_level = [this](float db) {
        last_level_db_ = db;
        const int64_t t = now_ms();
        if (t - last_level_ms_ >= 500) { // 500ms 节流更新电平显示
            last_level_ms_ = t;
            char buf[96];
            if (speaking_) {
                snprintf(buf, sizeof(buf), "识别中… %.0f dB", db);
            } else if (vad_ && t - last_asr_heartbeat_ms_ > 3000) {
                snprintf(buf, sizeof(buf), "识别引擎无响应…（查看 livesub.log）");
            } else {
                snprintf(buf, sizeof(buf), "就绪 %.0f dB（阈值 %.0f dB）",
                         db, vad_->current_threshold_db());
            }
            window_.set_status(buf);
        }
    };
    resample_buf_.resize((size_t)asr_.sample_rate() * 2); // 2s 缓冲

    // 5. 采集（--wav 测试模式下跳过真实麦克风）
    if (enable_capture) {
        WasapiCapture::Config cc;
        cc.device_id = cfg_.device_id;
        cc.boost_db  = cfg_.input_boost_db;
        std::string cerr;
        capture_.on_audio = [this](const float* pcm, size_t n, int64_t t_ms) {
            on_audio(pcm, n, t_ms);
        };
        if (!capture_.start(cc, &cerr)) {
            logf("[app] 麦克风采集失败: %s\n", cerr.c_str());
            window_.set_status("麦克风采集失败: " + cerr);
            update_tray(TrayIcon::State::Error, "LiveSub 麦克风失败: " + cerr);
            return false;
        }
    }

    // 6. ASR 线程
    queue_->start();
    asr_running_ = true;
    asr_thread_ = std::thread(&App::asr_loop, this);

    // 7. GPU/首次调用预热（编译 shader，避免首个窗口卡顿）
    {
        window_.set_status("预热推理管线…");
        std::vector<float> silence((size_t)asr_.sample_rate(), 0.0f); // 1s 静音
        AsrEngine::Result r = asr_.transcribe(silence.data(), silence.size());
        logf("[app] 预热完成 %s\n", r.ok ? "OK" : "失败");
    }

    window_.set_status("就绪 · 请说话");
    update_tray(TrayIcon::State::Ready, "LiveSub 就绪 · 请说话（双击设置）");
    if (enable_capture) {
        logf("[app] 启动完成：采集 %dHz → 识别 %dHz\n", capture_.sample_rate(), asr_.sample_rate());
    } else {
        logf("[app] 启动完成（wav 测试模式）\n");
    }
    return true;
}

bool App::run_wav_test(const std::string& wav_path) {
    // 读 wav 并重采样到识别采样率
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

    // 以 20ms 块实时节奏回放（模拟麦克风）
    const size_t block = (size_t)target * 20 / 1000;
    size_t off = 0;
    while (off < pcm16.size() && asr_running_) {
        const size_t n = std::min(block, pcm16.size() - off);
        vad_->process(pcm16.data() + off, n, 0);
        queue_->push(pcm16.data() + off, n);
        off += n;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    // 语音收尾：强制结束语音段（确保最后一句话定稿）
    if (vad_) vad_->force_end(now_ms());
    const int64_t t_end = now_ms() + 6000;
    while (now_ms() < t_end && asr_running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    logf("[app] wav 测试结束\n");
    return true;
}

void App::on_audio(const float* pcm, size_t n, int64_t t_ms) {
    if (!resampler_ || !queue_) return;
    // 48k → 16k
    const size_t n16 = resampler_->process(pcm, n, resample_buf_.data(), resample_buf_.size());
    if (n16 == 0) return;
    // VAD（16k 域）
    if (vad_) vad_->process(resample_buf_.data(), n16, t_ms);
    // 入队（环形缓冲会自动丢弃过旧数据）
    queue_->push(resample_buf_.data(), n16);
}

void App::asr_loop() {
    while (asr_running_) {
        if (!queue_->wait_for_window()) break;
        if (!asr_running_) break;

        const int64_t t_now = now_ms();
        const bool finalize = finalize_pending_.exchange(false);
        // 完全静音且无定稿请求 → 跳过
        if (!speaking_ && !finalize) {
            last_asr_heartbeat_ms_ = t_now;
            continue;
        }
        // 数据增量不足一个 hop（超时醒来）且非定稿 → 跳过，避免重复识别
        const size_t total_now = queue_->total_samples();
        const size_t hop_samples = (size_t)asr_.sample_rate() * cfg_.hop_ms / 1000;
        if (!finalize && total_now - last_processed_total_.load() < hop_samples) {
            last_asr_heartbeat_ms_ = t_now;
            continue;
        }
        last_processed_total_ = total_now;

        // 新语音段开始：清空历史（旧句子滚出，字幕只显示当前段内容）
        const size_t seg_now = seg_start_total_.load();
        if (!finalize && seg_now != last_seg_start_) {
            last_seg_start_ = seg_now;
            merger_.clear();
            window_.set_text("");
        }
        // 新段首窗最短 1.5s：太短的窗口识别质量差（碎片闪现），等积累够再识别
        if (!finalize &&
            total_now - seg_now < (size_t)asr_.sample_rate() * 3 / 2) {
            last_asr_heartbeat_ms_ = t_now;
            continue;
        }

        // 窗口 = 从语音段起点到段尾（VAD 分段驱动）：
        // 窗口对齐句子开头，识别内容始终是"这句开头到现在"，
        // 从根本上避免跨句混合（"旧句尾+新句头"）问题
        // finalize 时用固定段尾（on_speech_end 记录），避免新段开始后窗口漂移
        const size_t seg_start = seg_start_total_.load();
        const size_t seg_end = finalize ? seg_end_total_.load() : 0;
        const size_t max_len = (size_t)asr_.sample_rate() * 8; // 最长 8s（模型动态窗口上限）
        const size_t n = queue_->take_segment(seg_start, seg_end, max_len, win_buf_);
        if (n == 0) {
            last_asr_heartbeat_ms_ = t_now;
            continue;
        }

        // 窗口活跃帧占比（超过阈值-6dB 的 20ms 帧比例），防噪音误触发
        float active_ratio = 0.0f;
        {
            const float th = vad_->current_threshold_db() - 6.0f;
            const size_t frame = (size_t)asr_.sample_rate() * 20 / 1000;
            int active = 0, frames = 0;
            for (size_t i = 0; i + frame <= n; i += frame) {
                double sum = 0.0;
                for (size_t j = 0; j < frame; j++) {
                    sum += (double)win_buf_[i + j] * win_buf_[i + j];
                }
                const float db = 20.0f * (float)std::log10(std::sqrt(sum / frame) + 1e-12);
                if (db > th) active++;
                frames++;
            }
            if (frames > 0) active_ratio = (float)active / (float)frames;
        }
        if (!finalize && active_ratio < 0.15f) {
            // 语音段内无实际语音（噪音误触发 VAD）→ 跳过
            last_asr_heartbeat_ms_ = t_now;
            continue;
        }

        // 滚动缓冲：超 2 行时最旧句自然滚出
        merger_.prune(now_ms());

        const int64_t t0 = now_ms();
        AsrEngine::Result r;
        try {
            r = asr_.transcribe(win_buf_.data(), n);
        } catch (const std::exception& e) {
            // 引擎内部异常（如模型/GPU 问题）：记日志并继续，不杀 ASR 线程
            logf("[asr] 引擎异常: %s\n", e.what());
            window_.set_status("识别引擎异常: " + std::string(e.what()));
            update_tray(TrayIcon::State::Error, "LiveSub 识别引擎异常");
            last_asr_heartbeat_ms_ = now_ms();
            continue;
        }
        const int64_t cost = now_ms() - t0;
        last_asr_heartbeat_ms_ = now_ms();

        // 定稿判定：VAD 静音到达（语音段结束）
        const std::string full = merger_.update(r.text, finalize, now_ms());
        if (finalize) {
            last_finalize_ms_ = now_ms();
        }

        if (r.ok) {
            if (!r.text.empty()) {
                output_.update(full, finalize ? r.text : std::string());
                // 讲话稿记录：定稿句实时写入桌面文件
                if (finalize) {
                    output_.append_record(r.text);
                    // 语音输入：定稿句输入到当前焦点窗口（句末加空格）
                    if (voice_input_.enabled()) {
                        voice_input_.commit_text(r.text + " ");
                    }
                }
                window_.set_text(full);
                if (finalize) window_.set_status("");
            } else if (finalize) {
                // 定稿但无文本（可能只是噪音/气声）
                window_.set_status("未识别到语音（可调低 VAD 门限或靠近麦克风）");
            }
            if (cfg_.log_level >= 1) {
                logf("[asr] %s | enc=%lldms dec=%lldms total=%lldms%s\n",
                     r.text.empty() ? "(空)" : r.text.c_str(),
                     (long long)r.encode_ms, (long long)r.decode_ms, (long long)cost,
                     finalize ? " [FINAL]" : "");
            }
        } else {
            window_.set_status("识别错误: " + asr_.last_error());
            update_tray(TrayIcon::State::Error, "LiveSub 识别错误: " + asr_.last_error());
            if (cfg_.log_level >= 1) {
                logf("[asr] 识别失败: %s\n", asr_.last_error().c_str());
            }
        }
    }
}

void App::apply_config() {
    // 设置窗已写入 cfg_；应用需要重建的组件
    cfg_.save(cfg_.path());

    // 1. 停掉 ASR 线程（它可能正阻塞在旧队列的条件变量上，
    //    必须先 join 再重建队列，否则悬空引用导致线程挂起）
    asr_running_ = false;
    if (queue_) queue_->stop();
    if (asr_thread_.joinable()) asr_thread_.join();

    // 2. VAD / 分窗参数
    if (vad_) {
        Vad::Params vp;
        vp.sample_rate   = asr_.sample_rate();
        vp.threshold_db  = cfg_.vad_threshold_db;
        vp.margin_db     = cfg_.vad_margin_db;
        vp.min_speech_ms = cfg_.min_speech_ms;
        vp.silence_ms    = cfg_.silence_ms;
        vad_->set_params(vp);
    }
    // 队列重建
    if (queue_) {
        delete queue_;
    }
    queue_ = new AudioQueue(asr_.sample_rate(), cfg_.chunk_ms * 3, cfg_.chunk_ms, cfg_.hop_ms);

    // 3. 字幕样式
    SubtitleWindow::Style st;
    st.font_family   = utf8_to_wide(cfg_.font_family);
    st.font_size     = cfg_.font_size;
    st.font_color    = parse_color(cfg_.font_color).value_or(0xFFFFFFFF);
    st.bg_color      = parse_color(cfg_.bg_color).value_or(0xC0000000);
    st.window_w      = cfg_.window_w;
    st.window_h      = cfg_.window_h;
    // 百分比定位：pos_x/pos_y 为屏幕宽高的百分比（50=居中，越大越靠右/下）
    st.window_x      = GetSystemMetrics(SM_CXSCREEN) * cfg_.pos_x / 100 - cfg_.window_w / 2;
    st.window_y      = GetSystemMetrics(SM_CYSCREEN) * cfg_.pos_y / 100 - cfg_.window_h / 2;
    st.max_lines     = cfg_.max_lines;
    st.always_on_top = cfg_.always_on_top;
    st.click_through = cfg_.click_through;
    st.show_status   = cfg_.show_status;
    st.fade_in_ms    = cfg_.fade_in_ms;
    st.fade_out_ms   = cfg_.fade_out_ms;
    st.fps           = cfg_.fps;
    window_.destroy();
    std::wstring err;
    window_.create(st, &err);

    TextOutput::Config oc;
    oc.write_text = cfg_.write_text;
    oc.text_path  = cfg_.text_path;
    oc.write_srt  = cfg_.write_srt;
    oc.srt_path   = cfg_.srt_path;
    output_.configure(oc);

    // 4. 重启 ASR 线程
    merger_.clear();
    seg_start_total_ = 0;
    queue_->start();
    asr_running_ = true;
    asr_thread_ = std::thread(&App::asr_loop, this);
    window_.set_status("设置已应用 · 就绪");
    update_tray(TrayIcon::State::Ready, "LiveSub 设置已应用");
    logf("[app] 设置已应用\n");
}

void App::open_settings() {
    SettingsWindow win(cfg_,
                       [this]() { apply_config(); },
                       []() {});
    win.run();
}

void App::run() {
    // 主线程消息循环（字幕窗口 + 热键）
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void App::shutdown() {
    if (shutdown_done_.exchange(true)) return;
    asr_running_ = false;
    if (queue_) queue_->stop();
    if (asr_thread_.joinable()) asr_thread_.join();
    capture_.stop();
    if (vad_) { delete vad_; vad_ = nullptr; }
    if (queue_) { delete queue_; queue_ = nullptr; }
    if (resampler_) { delete resampler_; resampler_ = nullptr; }
    asr_.free();
    output_.clear();
    window_.destroy();
    tray_.destroy();
    tray_ready_ = false;
}
