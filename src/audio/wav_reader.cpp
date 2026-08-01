#include "wav_reader.h"

#include <cstdio>
#include <cstring>
#include <cstdint>

bool read_wav_mono(const std::string& path, std::vector<float>& pcm, int& sample_rate) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;

    char hdr[44];
    if (fread(hdr, 1, 44, f) != 44) { fclose(f); return false; }
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) { fclose(f); return false; }

    const uint16_t audio_fmt = *(uint16_t*)(hdr + 20);
    const uint16_t channels  = *(uint16_t*)(hdr + 22);
    sample_rate = *(int32_t*)(hdr + 24);
    const uint16_t bits      = *(uint16_t*)(hdr + 34);

    // 定位 data chunk
    long data_pos = -1;
    uint32_t data_size = 0;
    uint8_t chunk[8];
    fseek(f, 12, SEEK_SET);
    while (fread(chunk, 1, 8, f) == 8) {
        const uint32_t sz = *(uint32_t*)(chunk + 4);
        if (memcmp(chunk, "data", 4) == 0) {
            data_pos = ftell(f);
            data_size = sz;
            break;
        }
        fseek(f, sz + (sz & 1), SEEK_CUR);
    }
    if (data_pos < 0) { fclose(f); return false; }

    fseek(f, data_pos, SEEK_SET);
    std::vector<uint8_t> raw(data_size);
    if (fread(raw.data(), 1, data_size, f) != data_size) { fclose(f); return false; }
    fclose(f);

    const size_t n_samples = data_size / (channels * (bits / 8));
    pcm.resize(n_samples);
    const size_t bytes_per_frame = (size_t)channels * (bits / 8);
    for (size_t i = 0; i < n_samples; i++) {
        float v = 0.0f;
        if (audio_fmt == 3 && bits == 32) {
            v = *(const float*)(raw.data() + i * bytes_per_frame);
        } else if (bits == 16) {
            v = (float)(*(const int16_t*)(raw.data() + i * bytes_per_frame)) / 32768.0f;
        } else if (bits == 24) {
            const uint8_t* p = raw.data() + i * bytes_per_frame;
            int32_t s = (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16));
            if (s & 0x800000) s |= ~0xFFFFFF;
            v = (float)s / 8388608.0f;
        }
        // 多声道平均
        for (uint16_t c = 1; c < channels; c++) {
            float vc = 0.0f;
            const uint8_t* p = raw.data() + i * bytes_per_frame + c * (bits / 8);
            if (audio_fmt == 3 && bits == 32) {
                vc = *(const float*)p;
            } else if (bits == 16) {
                vc = (float)(*(const int16_t*)p) / 32768.0f;
            } else if (bits == 24) {
                int32_t s = (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16));
                if (s & 0x800000) s |= ~0xFFFFFF;
                vc = (float)s / 8388608.0f;
            }
            v += vc;
        }
        if (channels > 1) v /= (float)channels;
        pcm[i] = v;
    }
    return true;
}
