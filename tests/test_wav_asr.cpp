// 离线 ASR 测试：读取 WAV（PCM16/float，自动重采样到 16k）→ 转写 → 打印结果
// 用法: test_wav_asr <model.gguf> <mmproj.gguf> <input.wav> [--gpu N]
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>

#include "asr/asr_engine.h"
#include "audio/resampler.h"

// 极简 WAV 读取（PCM16 或 IEEE float32，单/双声道）
static bool read_wav(const std::string& path, std::vector<float>& out, int& sample_rate) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;

    char hdr[44];
    if (fread(hdr, 1, 44, f) != 44) { fclose(f); return false; }
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) { fclose(f); return false; }

    const uint16_t audio_fmt = *(uint16_t*)(hdr + 20);
    const uint16_t channels  = *(uint16_t*)(hdr + 22);
    sample_rate = *(int32_t*)(hdr + 24);
    const uint16_t bits      = *(uint16_t*)(hdr + 34);

    // 找到 data chunk
    long data_pos = -1;
    uint32_t data_size = 0;
    uint8_t chunk[8];
    long pos = 12;
    fseek(f, 12, SEEK_SET);
    while (fread(chunk, 1, 8, f) == 8) {
        const uint32_t sz = *(uint32_t*)(chunk + 4);
        if (memcmp(chunk, "data", 4) == 0) {
            data_pos = ftell(f);
            data_size = sz;
            break;
        }
        fseek(f, sz + (sz & 1), SEEK_CUR);
        pos = ftell(f);
    }
    if (data_pos < 0) { fclose(f); return false; }
    (void)pos;

    fseek(f, data_pos, SEEK_SET);
    std::vector<uint8_t> raw(data_size);
    if (fread(raw.data(), 1, data_size, f) != data_size) { fclose(f); return false; }
    fclose(f);

    const size_t n_samples = data_size / (channels * (bits / 8));
    out.resize(n_samples);
    for (size_t i = 0; i < n_samples; i++) {
        float v = 0.0f;
        if (audio_fmt == 3 && bits == 32) {
            // IEEE float
            const float s = *(const float*)(raw.data() + i * channels * 4);
            v = s;
        } else if (bits == 16) {
            const int16_t s = *(const int16_t*)(raw.data() + i * channels * 2);
            v = (float)s / 32768.0f;
        } else if (bits == 24) {
            const uint8_t* p = raw.data() + i * channels * 3;
            int32_t s = (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16));
            if (s & 0x800000) s |= ~0xFFFFFF;
            v = (float)s / 8388608.0f;
        }
        // 多声道平均
        for (uint16_t c = 1; c < channels; c++) {
            float vc = 0.0f;
            if (audio_fmt == 3 && bits == 32) {
                vc = *(const float*)(raw.data() + (i * channels + c) * 4);
            } else if (bits == 16) {
                vc = (float)(*(const int16_t*)(raw.data() + (i * channels + c) * 2)) / 32768.0f;
            }
            v += vc;
        }
        if (channels > 1) v /= (float)channels;
        out[i] = v;
    }
    return true;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "用法: test_wav_asr <model.gguf> <mmproj.gguf> <input.wav> [--gpu N] [-p prompt]\n");
        return 1;
    }
    const std::string model = argv[1];
    const std::string mmproj = argv[2];
    const std::string wav_path = argv[3];

    int gpu_layers = 0;
    std::string prompt = "Transcribe the audio.";
    for (int i = 4; i < argc; i++) {
        if (strcmp(argv[i], "--gpu") == 0 && i + 1 < argc) {
            gpu_layers = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            prompt = argv[++i];
        }
    }

    // 读 wav
    std::vector<float> pcm;
    int wav_rate = 0;
    if (!read_wav(wav_path, pcm, wav_rate)) {
        fprintf(stderr, "无法读取 WAV: %s\n", wav_path.c_str());
        return 1;
    }
    printf("音频: %s  %dHz  %.1fs  %zu 样本\n", wav_path.c_str(), wav_rate,
           (double)pcm.size() / wav_rate, pcm.size());

    // 初始化引擎
    AsrEngine engine;
    AsrEngineParams p;
    p.model_path = model;
    p.aux_path = mmproj;
    p.gpu_layers = gpu_layers;
    p.prompt = prompt;
    p.verbosity = 2;
    std::string err;
    if (!engine.init(p, &err)) {
        fprintf(stderr, "引擎初始化失败: %s\n", err.c_str());
        return 1;
    }

    // 重采样到引擎采样率
    std::vector<float> pcm16;
    if (wav_rate != engine.sample_rate()) {
        Resampler rs(wav_rate, engine.sample_rate());
        pcm16.resize(pcm.size() * engine.sample_rate() / wav_rate + 64);
        const size_t n = rs.process(pcm.data(), pcm.size(), pcm16.data(), pcm16.size());
        pcm16.resize(n);
        printf("重采样 %dHz → %dHz (%zu 样本)\n", wav_rate, engine.sample_rate(), n);
    } else {
        pcm16 = std::move(pcm);
    }

    // 转写
    AsrEngineResult r = engine.transcribe(pcm16.data(), pcm16.size());
    printf("\n===== 结果 =====\n");
    printf("语言: %s\n", r.language.empty() ? "(未检测)" : r.language.c_str());
    printf("文本: %s\n", r.text.c_str());
    printf("耗时: encode %lldms, decode %lldms\n",
           (long long)r.encode_ms, (long long)r.decode_ms);
    printf("原始: %s\n", r.raw.c_str());
    return r.ok && !r.text.empty() ? 0 : 1;
}
