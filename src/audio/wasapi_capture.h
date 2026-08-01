#pragma once
// WASAPI 共享模式麦克风采集（事件驱动，低延迟）
// 输出：float32 单声道，采样率 = mix format 采样率（通常 48kHz）
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <utility>

#include <windows.h>

struct IMMDeviceEnumerator;
struct IMMDevice;
struct IAudioClient;
struct IAudioCaptureClient;

class WasapiCapture {
public:
    struct Config {
        std::string device_id;   // 空 = 默认设备（输入/输出，取决于 loopback）
        float boost_db = 0.0f;   // 增益补偿（dB）
        bool loopback = false;   // true = 采集系统输出（电脑声音）；false = 麦克风
    };

    // on_audio(pcm, n_samples, t_ms)：t_ms 为单调时钟毫秒
    std::function<void(const float*, size_t, int64_t)> on_audio;

    bool start(const Config& cfg, std::string* err = nullptr);
    void stop();

    int sample_rate() const { return sample_rate_; }
    bool running() const { return running_; }

    // 枚举输入设备 (name, id)
    static std::vector<std::pair<std::string, std::string>> list_devices();

    WasapiCapture() = default;
    ~WasapiCapture() { stop(); }
    WasapiCapture(const WasapiCapture&) = delete;
    WasapiCapture& operator=(const WasapiCapture&) = delete;

private:
    void capture_loop();
    void release_com();

    Config cfg_;
    std::atomic<bool> running_{false};
    std::thread thread_;

    // COM 接口裸指针（CoInitializeEx 线程内初始化，手动释放）
    IMMDeviceEnumerator*  enumerator_ = nullptr;
    IMMDevice*            device_ = nullptr;
    IAudioClient*         audio_client_ = nullptr;
    IAudioCaptureClient*  capture_client_ = nullptr;
    HANDLE event_ = nullptr;

    int sample_rate_ = 0;
    int n_channels_ = 0;
    int bits_per_sample_ = 0;
    bool is_float_ = false;
    std::vector<float> mix_buf_;  // 混音缓冲
};
