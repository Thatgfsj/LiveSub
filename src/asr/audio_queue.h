#pragma once
// 流式音频队列：采集线程持续写入 16kHz 单声道 PCM；
// ASR 线程按 hop 间隔取出 [T-chunk, T] 滑动窗口
#include <cstddef>
#include <vector>
#include <mutex>
#include <condition_variable>

class AudioQueue {
public:
    // capacity_ms: 缓冲容量（应 ≥ chunk_ms + 余量）
    AudioQueue(int sample_rate, int capacity_ms, int chunk_ms, int hop_ms);

    // 采集线程：写入一块音频（返回实际写入样本数）
    size_t push(const float* pcm, size_t n);

    // ASR 线程：等待直到可处理一个新窗口
    // 返回 true 且有数据；false 表示停止
    bool wait_for_window();

    // 取出当前窗口（[now-chunk, now] 的滑动窗口；如果音频不足 chunk 则取全部）
    // 返回窗口样本数；window 容量由调用方按 chunk_ms 预留
    size_t take_window(std::vector<float>& window);

    // 取语音段窗口：[start_total, end_total) 的样本（最多 max_len 样本）。
    // end_total=0 表示取到当前。窗口起点对齐语音段起点（句子开头），
    // 从根本上避免窗口跨句混合内容。
    // 起点早于缓冲范围时从缓冲最早处开始；窗口过长时保留末尾 max_len 样本。
    size_t take_segment(size_t start_total, size_t end_total, size_t max_len, std::vector<float>& out);

    // 当前累计样本数（含未消费部分）
    size_t total_samples() const;
    int64_t current_ms() const { return total_ms_; }

    void start();
    void stop();

    // 复位（清空缓冲与累计时间）
    void reset();

private:
    int sr_;
    size_t capacity_;
    size_t chunk_;   // 窗口样本数
    size_t hop_;     // 步长样本数

    std::vector<float> buf_;       // 环形缓冲
    size_t head_ = 0;              // 写入位置
    size_t n_buffered_ = 0;        // 有效样本数
    size_t total_ = 0;             // 累计写入样本数
    size_t last_take_total_ = 0;   // 上次取窗口时的 total_
    int64_t total_ms_ = 0;

    bool running_ = false;
    mutable std::mutex mtx_;
    std::condition_variable cv_;
};
