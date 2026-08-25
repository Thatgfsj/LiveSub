#include "vad.h"

#include <cmath>
#include <algorithm>
#include <cstdio>

Vad::Vad(const Params& p) : p_(p) { reset(); }

void Vad::reset() {
    frame_len_ = std::max(1, p_.sample_rate * p_.frame_ms / 1000);
    buf_.clear();
    in_speech_ = false;
    speech_candidate_ms_ = -1;
    speech_start_ms_ = 0;
    last_voice_ms_ = 0;
    last_processed_ms_ = 0;
    noise_floor_db_ = -60.0f;
    smooth_db_ = -60.0f;
}

void Vad::process(const float* pcm, size_t n, int64_t t_ms) {
    buf_.insert(buf_.end(), pcm, pcm + n);
    const size_t n_frames = buf_.size() / frame_len_;
    if (n_frames == 0) return;
    for (size_t i = 0; i < n_frames; i++) {
        const int64_t frame_t = last_processed_ms_ + (int64_t)i * p_.frame_ms;
        feed_frame(buf_.data() + i * frame_len_, frame_t);
    }
    // 保留不足一帧的余数
    const size_t consumed = n_frames * frame_len_;
    if (consumed > 0) {
        buf_.erase(buf_.begin(), buf_.begin() + consumed);
        last_processed_ms_ += (int64_t)n_frames * p_.frame_ms;
    }
    (void)t_ms;
}

void Vad::feed_frame(const float* pcm, int64_t frame_t) {
    // 帧 RMS → dBFS
    double sum = 0.0;
    for (int j = 0; j < frame_len_; j++) {
        sum += (double)pcm[j] * pcm[j];
    }
    const double rms = std::sqrt(sum / frame_len_);
    const double rms_clamped = std::min(rms, (double)p_.max_rms);
    float db = 20.0f * (float)std::log10(rms_clamped + 1e-12);
    // 下限保护：数字静音（0 值）会把底噪估计拉到 -240dB，导致阈值失效
    if (db < -60.0f) db = -60.0f;

    // 平滑电平（界面显示）：指数平滑，约 0.3s 时间常数
    smooth_db_ = smooth_db_ + (db - smooth_db_) * 0.15f;
    if (on_level) on_level(smooth_db_);

    // 底噪自适应（最小值跟踪）：
    //   帧能量低于底噪 → 立即下降（跟随环境变化）
    //   高于底噪 → 极慢回升（约 30s 半衰），避免语音污染底噪
    if (db < noise_floor_db_) {
        noise_floor_db_ = db;
    } else {
        noise_floor_db_ += (db - noise_floor_db_) * 0.0004f;
    }

    const float threshold = (p_.threshold_db != 0.0f)
                                ? p_.threshold_db              // 手动门限
                                : noise_floor_db_ + p_.margin_db; // 自适应

    if (!in_speech_) {
        if (db > threshold) {
            // 语音持续 min_speech_ms 后才确认段开始（短促噪声不触发）
            if (speech_candidate_ms_ < 0) speech_candidate_ms_ = frame_t;
            if (frame_t - speech_candidate_ms_ >= p_.min_speech_ms) {
                in_speech_ = true;
                // pre-roll 回退 250ms：candidate 之前的低能量部分恰是首字的
                // 辅音/轻声起始，不回退会导致每句开头丢字（音频在环形缓冲
                // 容量内，回退的样本仍可取到）
                speech_start_ms_ = std::max((int64_t)0,
                                            speech_candidate_ms_ - 250);
                last_voice_ms_ = frame_t;
                if (on_speech_start) on_speech_start(speech_start_ms_);
            }
        } else {
            speech_candidate_ms_ = -1; // 噪声未持续 → 取消候选
        }
    } else {
        if (db > threshold - 6.0f) { // 迟滞 6dB
            last_voice_ms_ = frame_t;
        } else if (frame_t - last_voice_ms_ >= p_.silence_ms) {
            in_speech_ = false;
            if (on_speech_end) on_speech_end(last_voice_ms_ + p_.silence_ms);
        }
    }
}

void Vad::force_end(int64_t t_ms) {
    if (in_speech_) {
        in_speech_ = false;
        if (on_speech_end) on_speech_end(t_ms > 0 ? t_ms : last_voice_ms_);
    }
}
