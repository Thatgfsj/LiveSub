#pragma once
// 极简 WAV 读取（PCM16/24/float32，单/多声道 → float32 单声道）
#include <string>
#include <vector>

// 返回 false 表示失败；sample_rate 输出采样率
bool read_wav_mono(const std::string& path, std::vector<float>& pcm, int& sample_rate);
