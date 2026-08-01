// 重采样器单元测试
#include <cstdio>
#include <cmath>
#include <vector>

#include "audio/resampler.h"

static int failures = 0;

static void check(bool cond, const char* name) {
    if (cond) {
        printf("  PASS: %s\n", name);
    } else {
        printf("  FAIL: %s\n", name);
        failures++;
    }
}

int main() {
    const int src_rate = 48000, dst_rate = 16000;

    // 1. 正弦波重采样：频率保持正确（1kHz 正弦）
    {
        Resampler r(src_rate, dst_rate);
        std::vector<float> in(48000); // 1s
        const float f = 1000.0f;
        for (size_t i = 0; i < in.size(); i++) {
            in[i] = std::sin(2.0f * 3.14159265f * f * (float)i / (float)src_rate);
        }
        std::vector<float> out(in.size() * dst_rate / src_rate + 16);
        const size_t n = r.process(in.data(), in.size(), out.data(), out.size());
        check(n >= 15900 && n <= 16100, "输出长度 ≈ 输入/3 (16000±100)");
        // 过零率验证频率（16k 域 1kHz = 每 8 样本一周期，16 样本 2 个过零）
        int zero_crossings = 0;
        for (size_t i = 1; i < n; i++) {
            if ((out[i - 1] < 0 && out[i] >= 0) || (out[i - 1] >= 0 && out[i] < 0)) zero_crossings++;
        }
        // 1 秒内 1000 个周期 = 2000 次过零（±5%）
        check(std::abs(zero_crossings - 2000) < 100, "过零率 ≈ 2kHz (频率保持正确)");
    }

    // 2. 直流信号幅度保持
    {
        Resampler r(src_rate, dst_rate);
        std::vector<float> in(9600, 0.5f);
        std::vector<float> out(in.size() / 3 + 16);
        const size_t n = r.process(in.data(), in.size(), out.data(), out.size());
        bool ok = n > 0;
        for (size_t i = 0; i < n; i++) {
            if (std::fabs(out[i] - 0.5f) > 1e-3f) { ok = false; break; }
        }
        check(ok, "直流信号幅度保持");
    }

    // 3. 分块调用与整块调用结果一致
    {
        std::vector<float> in(48000);
        const float f = 440.0f;
        for (size_t i = 0; i < in.size(); i++) {
            in[i] = std::sin(2.0f * 3.14159265f * f * (float)i / (float)src_rate);
        }
        // 整块
        Resampler r1(src_rate, dst_rate);
        std::vector<float> out1(in.size() / 3 + 64);
        const size_t n1 = r1.process(in.data(), in.size(), out1.data(), out1.size());
        // 分块（每 1024 样本）
        Resampler r2(src_rate, dst_rate);
        std::vector<float> out2(in.size() / 3 + 64);
        size_t n2 = 0;
        for (size_t off = 0; off < in.size(); off += 1024) {
            const size_t block = std::min((size_t)1024, in.size() - off);
            n2 += r2.process(in.data() + off, block, out2.data() + n2, out2.size() - n2);
        }
        // 比较重叠区域（前 8000 样本）
        bool same = n1 == n2;
        if (same) {
            for (size_t i = 0; i < std::min(n1, (size_t)8000); i++) {
                if (std::fabs(out1[i] - out2[i]) > 1e-3f) { same = false; break; }
            }
        }
        check(same, "分块调用与整块调用一致");
    }

    // 4. 极端比率（16k → 48k 上采样）
    {
        Resampler r(16000, 48000);
        std::vector<float> in(16000);
        for (size_t i = 0; i < in.size(); i++) in[i] = 0.3f;
        std::vector<float> out(16000 * 3 + 16);
        const size_t n = r.process(in.data(), in.size(), out.data(), out.size());
        check(n >= 47800 && n <= 48200, "上采样 16k→48k 长度正确");
    }

    printf(failures == 0 ? "\n全部通过\n" : "\n%d 项失败\n", failures);
    return failures == 0 ? 0 : 1;
}
