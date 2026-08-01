#pragma once
// 线性插值重采样器（支持任意有理采样率比，48k→16k 等）
// 输入输出均为 float32 单声道
#include <cstddef>

class Resampler {
public:
    // src_rate: 输入采样率, dst_rate: 输出采样率
    Resampler(int src_rate, int dst_rate);

    // 输入 n_in 个采样，返回重采样后的样本（内部缓冲，调用方需在下次调用前消费）
    // 返回输出样本数；out 容量需 >= 所需最大输出（n_in * dst_rate / src_rate + 2）
    size_t process(const float* in, size_t n_in, float* out, size_t out_cap);

    // 冲洗内部余数（结尾用）
    size_t flush(float* out, size_t out_cap);

    void reset();

    int src_rate() const { return src_rate_; }
    int dst_rate() const { return dst_rate_; }

private:
    int src_rate_, dst_rate_;
    float last_ = 0.0f;     // 上一块末尾样本（块内坐标 0）
    bool  has_last_ = false;
    double cur_ = 1.0;      // 下一个输出点的块内坐标（in[i] 位于 i+1）
};
