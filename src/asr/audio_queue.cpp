#include "audio_queue.h"

#include <algorithm>
#include <chrono>
#include <cstring>

AudioQueue::AudioQueue(int sample_rate, int capacity_ms, int chunk_ms, int hop_ms)
    : sr_(sample_rate) {
    capacity_ = (size_t)((int64_t)sample_rate * capacity_ms / 1000);
    chunk_    = (size_t)((int64_t)sample_rate * chunk_ms / 1000);
    hop_      = (size_t)((int64_t)sample_rate * hop_ms / 1000);
    if (hop_ == 0) hop_ = 1;
    buf_.resize(capacity_);
    reset();
}

void AudioQueue::start() {
    std::lock_guard<std::mutex> lk(mtx_);
    running_ = true;
    cv_.notify_all();
}

void AudioQueue::stop() {
    std::lock_guard<std::mutex> lk(mtx_);
    running_ = false;
    cv_.notify_all();
}

void AudioQueue::reset() {
    std::lock_guard<std::mutex> lk(mtx_);
    head_ = 0;
    n_buffered_ = 0;
    total_ = 0;
    last_take_total_ = 0;
    total_ms_ = 0;
}

size_t AudioQueue::push(const float* pcm, size_t n) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!running_ || n == 0) return 0;

    // 总是写入：缓冲写满后覆盖最旧数据（环形缓冲语义）
    // ★ total_ 必须持续增长，否则 ASR 线程的 wait_for_window 将永远等不到新窗口
    for (size_t i = 0; i < n; i++) {
        buf_[(head_ + i) % capacity_] = pcm[i];
    }
    head_ = (head_ + n) % capacity_;
    n_buffered_ = std::min(n_buffered_ + n, capacity_);
    total_ += n;
    total_ms_ = (int64_t)(total_ * 1000 / sr_);
    cv_.notify_one();
    return n;
}

bool AudioQueue::wait_for_window() {
    std::unique_lock<std::mutex> lk(mtx_);
    // 等待：新数据达到一个 hop 步长；带 200ms 超时，
    // 让 ASR 线程能定期醒来检查定稿请求（音频停止后 VAD 段结束的 finalize）
    cv_.wait_for(lk, std::chrono::milliseconds(200), [&] {
        if (!running_) return true;
        if (n_buffered_ == 0) return false;
        // 是否跨越了新的 hop 边界
        const size_t target = last_take_total_ + hop_;
        return total_ >= target && total_ > last_take_total_;
    });
    return running_;
}

size_t AudioQueue::take_segment(size_t start_total, size_t end_total, size_t max_len, std::vector<float>& out) {
    std::lock_guard<std::mutex> lk(mtx_);
    const size_t end = (end_total == 0) ? total_ : std::min(end_total, total_);
    if (start_total >= end) return 0;

    // 缓冲内可用范围 [total_-n_buffered_, total_)
    size_t start = start_total;
    if (total_ > n_buffered_ && start < total_ - n_buffered_) {
        start = total_ - n_buffered_; // 起点太早（被覆盖）→ 从缓冲最早处开始
    }
    if (start >= end) return 0;
    // 窗口过长时保留末尾 max_len 样本
    if (max_len > 0 && end - start > max_len) {
        start = end - max_len;
    }
    const size_t n = end - start;
    out.resize(n);
    // 全局样本序号 s（∈[start,end)）在环形缓冲中的位置：
    //   样本 total_-1 位于 (head_-1+cap)%cap，依次向前
    for (size_t i = 0; i < n; i++) {
        const size_t s = start + i;
        const size_t pos = (head_ + capacity_ - ((total_ - s) % capacity_)) % capacity_;
        out[i] = buf_[pos];
    }
    last_take_total_ = total_;
    return n;
}

size_t AudioQueue::total_samples() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return total_;
}
