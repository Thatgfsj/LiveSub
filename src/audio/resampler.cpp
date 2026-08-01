#include "resampler.h"

#include <cmath>
#include <cstddef>

// 线性插值重采样（游标模型）：
//   step = src_rate / dst_rate，输出第 k 个样本位于输入时间轴 k*step 处
//   块内坐标系：in[i] 位于位置 i+1，last_（上一块末尾样本）位于位置 0
//   输出点位置 p：p<1 时插值 (last_, in[0])；否则插值 (in[i0], in[i0+1])
//   需要 in[n_in] 时（p-1 >= n_in-1）暂停，块尾折算：cur_ -= n_in（last_ 成为新块位置 0）
// 调用方需保证 out 容量 >= n_in * dst_rate / src_rate + 2

Resampler::Resampler(int src_rate, int dst_rate)
    : src_rate_(src_rate), dst_rate_(dst_rate) {
    reset();
}

void Resampler::reset() {
    last_ = 0.0f;
    has_last_ = false;
    cur_ = 1.0; // 首个输出点 = in[0] 本身
}

size_t Resampler::process(const float* in, size_t n_in, float* out, size_t out_cap) {
    if (n_in == 0 || out_cap == 0) return 0;
    const double step = (double)src_rate_ / (double)dst_rate_;
    size_t n_out = 0;

    while (n_out < out_cap) {
        const double p = cur_;
        if (p < 1.0) {
            // 插值 last_（位置 0）与 in[0]（位置 1）
            const double f = p; // f=0 → last_，f=1 → in[0]
            out[n_out++] = (float)(last_ * (1.0 - f) + in[0] * f);
        } else {
            const size_t i0 = (size_t)(p - 1.0); // 对应 in[i0]
            if (i0 + 1 >= n_in) break;           // 需要 in[n_in] → 留到下一块
            const double f = p - 1.0 - (double)i0;
            out[n_out++] = (float)(in[i0] * (1.0 - f) + in[i0 + 1] * f);
        }
        cur_ += step;
    }

    if (n_in > 0) {
        last_ = in[n_in - 1];
        has_last_ = true;
    }
    // 折算：cur_ 落在 [n_in, n_in+1) 时，last_ 成为新块位置 0
    if (cur_ >= (double)n_in) {
        cur_ -= (double)n_in;
    }
    return n_out;
}

size_t Resampler::flush(float* out, size_t out_cap) {
    // 尾部不完整输入直接丢弃（误差 < 1 采样周期）
    (void)out; (void)out_cap;
    has_last_ = false;
    cur_ = 1.0;
    return 0;
}
