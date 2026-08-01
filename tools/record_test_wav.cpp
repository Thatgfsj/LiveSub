// 录制麦克风到 WAV（用于离线测试/验证）
// 用法: record_test_wav <out.wav> [seconds] [device_id]
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#include "audio/wasapi_capture.h"

int main(int argc, char** argv) {
    const std::string out_path = argc > 1 ? argv[1] : "test_recording.wav";
    const int seconds = argc > 2 ? atoi(argv[2]) : 8;
    const std::string device = argc > 3 ? argv[3] : "";

    printf("录音 %d 秒 → %s （说点中文测试语音…）\n", seconds, out_path.c_str());

    // WAV 头（44 字节 PCM16 单声道 16k，先写占位，结束后回填）
    FILE* f = fopen(out_path.c_str(), "wb");
    if (!f) { fprintf(stderr, "无法创建输出文件\n"); return 1; }
    const int sr = 16000;
    const int bits = 16, ch = 1;
    uint8_t hdr[44] = {};
    memcpy(hdr, "RIFF", 4);
    memcpy(hdr + 8, "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);
    *(uint32_t*)(hdr + 16) = 16;
    *(uint16_t*)(hdr + 20) = 1;          // PCM
    *(uint16_t*)(hdr + 22) = (uint16_t)ch;
    *(uint32_t*)(hdr + 24) = (uint32_t)sr;
    *(uint32_t*)(hdr + 28) = (uint32_t)(sr * ch * bits / 8);
    *(uint16_t*)(hdr + 32) = (uint16_t)(ch * bits / 8);
    *(uint16_t*)(hdr + 34) = (uint16_t)bits;
    memcpy(hdr + 36, "data", 4);
    fwrite(hdr, 1, 44, f);

    WasapiCapture cap;
    std::atomic<bool> done{false};
    int64_t t0 = -1;
    std::vector<int16_t> pcm;
    cap.on_audio = [&](const float* data, size_t n, int64_t t_ms) {
        if (t0 < 0) t0 = t_ms;
        if (t_ms - t0 > (int64_t)seconds * 1000) { done = true; return; }
        // float → int16
        for (size_t i = 0; i < n; i++) {
            float v = data[i] * 32768.0f;
            if (v > 32767.0f) v = 32767.0f;
            if (v < -32768.0f) v = -32768.0f;
            pcm.push_back((int16_t)v);
        }
    };

    WasapiCapture::Config cfg;
    cfg.device_id = device;
    std::string err;
    if (!cap.start(cfg, &err)) {
        fprintf(stderr, "采集失败: %s\n", err.c_str());
        return 1;
    }
    printf("采集率: %dHz（将线性下采样到 16k 写入）\n", cap.sample_rate());

    // 等待完成
    while (!done) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    cap.stop();

    // 简化：如果采集率 != 16k，做线性抽取（工具用途，精度可接受）
    const int cap_rate = cap.sample_rate();
    const size_t step = (size_t)std::max(1, cap_rate / sr);
    for (size_t i = 0; i < pcm.size(); i += step) {
        fwrite(&pcm[i], 2, 1, f);
    }
    const uint32_t data_bytes = (uint32_t)(pcm.size() / step) * 2;
    fseek(f, 4, SEEK_SET);
    *(uint32_t*)(hdr + 4) = 36 + data_bytes;
    *(uint32_t*)(hdr + 40) = data_bytes;
    fwrite(hdr, 1, 44, f);
    fclose(f);
    printf("完成：%s（%.1f 秒，%d 样本）\n", out_path.c_str(),
           (double)(pcm.size() / step) / sr, (int)(pcm.size() / step));
    return 0;
}
