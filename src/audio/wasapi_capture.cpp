#include "wasapi_capture.h"

#include <cmath>
#include <cstring>
#include <algorithm>

#define INITGUID
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#undef INITGUID

// KSDATAFORMAT_SUBTYPE_* 在 MinGW 下未导出，手动定义（标准音频 GUID）
static const GUID GUID_SUBTYPE_IEEE_FLOAT =
    {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
static const GUID GUID_SUBTYPE_PCM =
    {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

// ---------------------------------------------------------------------------
// COM 工具
// ---------------------------------------------------------------------------
static bool com_ok(HRESULT hr) {
    return SUCCEEDED(hr);
}

static std::string utf8_from_wide(const wchar_t* w) {
    if (!w) return "";
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return "";
    std::string s((size_t)n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

static std::wstring wide_from_utf8(const std::string& s) {
    if (s.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w((size_t)n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

// ---------------------------------------------------------------------------
// 设备枚举
// ---------------------------------------------------------------------------
std::vector<std::pair<std::string, std::string>> WasapiCapture::list_devices() {
    std::vector<std::pair<std::string, std::string>> out;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool need_uninit = com_ok(hr);
    IMMDeviceEnumerator* enumerator = nullptr;
    if (com_ok(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator), (void**)&enumerator))) {
        IMMDeviceCollection* coll = nullptr;
        if (com_ok(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &coll))) {
            UINT n = 0;
            coll->GetCount(&n);
            for (UINT i = 0; i < n; i++) {
                IMMDevice* dev = nullptr;
                if (com_ok(coll->Item(i, &dev))) {
                    IPropertyStore* props = nullptr;
                    if (com_ok(dev->OpenPropertyStore(STGM_READ, &props))) {
                        PROPVARIANT pv;
                        PropVariantInit(&pv);
                        if (com_ok(props->GetValue(PKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR) {
                            LPWSTR id = nullptr;
                            dev->GetId(&id);
                            out.emplace_back(utf8_from_wide(pv.pwszVal), id ? utf8_from_wide(id) : "");
                            CoTaskMemFree(id);
                        }
                        PropVariantClear(&pv);
                        props->Release();
                    }
                    dev->Release();
                }
            }
            coll->Release();
        }
        enumerator->Release();
    }
    if (need_uninit) CoUninitialize();
    return out;
}

// ---------------------------------------------------------------------------
// 启动
// ---------------------------------------------------------------------------
bool WasapiCapture::start(const Config& cfg, std::string* err) {
    stop();
    cfg_ = cfg;

    auto fail = [&](const std::string& e) {
        if (err) *err = e;
        release_com();
        return false;
    };

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (!com_ok(hr) && hr != RPC_E_CHANGED_MODE) {
        return fail("COM 初始化失败");
    }

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), (void**)&enumerator_);
    if (!com_ok(hr)) return fail("无法创建设备枚举器");

    const EDataFlow flow = cfg_.loopback ? eRender : eCapture;
    if (cfg_.device_id.empty()) {
        if (cfg_.loopback) {
            // 电脑声音：默认输出设备 + loopback
            hr = enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device_);
            if (!com_ok(hr)) return fail("无法获取默认输出设备（电脑声音）");
        } else {
            hr = enumerator_->GetDefaultAudioEndpoint(eCapture, eCommunications, &device_);
            if (!com_ok(hr)) {
                hr = enumerator_->GetDefaultAudioEndpoint(eCapture, eConsole, &device_);
            }
            if (!com_ok(hr)) return fail("无法获取默认麦克风（请检查输入设备）");
        }
    } else {
        // 按 ID 精确匹配，失败则按名称模糊匹配
        IMMDeviceCollection* coll = nullptr;
        if (com_ok(enumerator_->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &coll))) {
            UINT n = 0;
            coll->GetCount(&n);
            std::wstring target = wide_from_utf8(cfg_.device_id);
            for (UINT i = 0; i < n && !device_; i++) {
                IMMDevice* dev = nullptr;
                if (com_ok(coll->Item(i, &dev))) {
                    LPWSTR id = nullptr;
                    dev->GetId(&id);
                    IPropertyStore* props = nullptr;
                    if (id && std::wstring(id) == target) {
                        device_ = dev;
                        dev->AddRef();
                    } else if (com_ok(dev->OpenPropertyStore(STGM_READ, &props))) {
                        PROPVARIANT pv;
                        PropVariantInit(&pv);
                        if (com_ok(props->GetValue(PKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR &&
                            std::wstring(pv.pwszVal).find(target) != std::wstring::npos) {
                            device_ = dev;
                            dev->AddRef();
                        }
                        PropVariantClear(&pv);
                        props->Release();
                    }
                    CoTaskMemFree(id);
                    dev->Release();
                }
            }
            coll->Release();
        }
        if (!device_) return fail("找不到指定麦克风: " + cfg_.device_id);
    }

    // 激活音频客户端
    hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audio_client_);
    if (!com_ok(hr)) return fail("无法激活音频客户端");

    // 设备混音格式
    WAVEFORMATEX* wf = nullptr;
    hr = audio_client_->GetMixFormat(&wf);
    if (!com_ok(hr) || !wf) return fail("无法获取混音格式");

    sample_rate_ = (int)wf->nSamplesPerSec;
    n_channels_ = (int)wf->nChannels;
    bits_per_sample_ = (int)wf->wBitsPerSample;

    WAVEFORMATEXTENSIBLE* wfe = (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE) ? (WAVEFORMATEXTENSIBLE*)wf : nullptr;
    is_float_ = false;
    if (wfe) {
        if (IsEqualGUID(wfe->SubFormat, GUID_SUBTYPE_IEEE_FLOAT)) is_float_ = true;
        if (IsEqualGUID(wfe->SubFormat, GUID_SUBTYPE_PCM)) is_float_ = false;
    } else {
        is_float_ = (wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
    }

    // 共享模式 + 事件驱动（显式传入混音格式，避免 E_POINTER）
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event_) {
        CoTaskMemFree(wf);
        return fail("创建事件失败");
    }

    const REFERENCE_TIME buf_duration = 200000; // 20ms 共享缓冲
    DWORD stream_flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    if (cfg_.loopback) stream_flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
    hr = audio_client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                   stream_flags,
                                   buf_duration, 0, wf, nullptr);
    CoTaskMemFree(wf);
    if (!com_ok(hr)) {
        char msg[128];
        snprintf(msg, sizeof(msg), "音频客户端初始化失败（共享模式）HRESULT=0x%08lX", (unsigned long)hr);
        return fail(msg);
    }

    hr = audio_client_->SetEventHandle(event_);
    if (!com_ok(hr)) return fail("设置事件句柄失败");

    hr = audio_client_->GetService(__uuidof(IAudioCaptureClient), (void**)&capture_client_);
    if (!com_ok(hr)) return fail("获取采集客户端失败");

    hr = audio_client_->Start();
    if (!com_ok(hr)) return fail("采集启动失败");

    running_ = true;
    thread_ = std::thread(&WasapiCapture::capture_loop, this);
    return true;
}

void WasapiCapture::stop() {
    if (running_) {
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }
    if (audio_client_) {
        audio_client_->Stop();
    }
    release_com();
}

void WasapiCapture::release_com() {
    if (capture_client_) { capture_client_->Release(); capture_client_ = nullptr; }
    if (audio_client_)   { audio_client_->Release();   audio_client_   = nullptr; }
    if (device_)         { device_->Release();         device_         = nullptr; }
    if (enumerator_)     { enumerator_->Release();     enumerator_     = nullptr; }
    if (event_) { CloseHandle(event_); event_ = nullptr; }
    CoUninitialize();
}

// ---------------------------------------------------------------------------
// 采集循环
// ---------------------------------------------------------------------------
void WasapiCapture::capture_loop() {
    mix_buf_.resize(4096 * 8);

    while (running_) {
        // 等待数据事件（20ms 周期）
        DWORD wait = WaitForSingleObject(event_, 1000);
        if (wait == WAIT_TIMEOUT) continue;
        if (!running_) break;

        UINT32 packet = 0;
        while (audio_client_ && capture_client_ &&
               com_ok(capture_client_->GetNextPacketSize(&packet)) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            HRESULT hr = capture_client_->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if (!com_ok(hr)) break;

            if (frames > 0 && data && !(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                // 逐帧转换 → float mono
                const size_t n_out = (size_t)frames;
                if (n_out > mix_buf_.size()) mix_buf_.resize(n_out);
                float* out = mix_buf_.data();

                if (is_float_ && bits_per_sample_ == 32) {
                    const float* src = (const float*)data;
                    for (UINT32 f = 0; f < frames; f++) {
                        float v = 0.0f;
                        for (int c = 0; c < n_channels_; c++) v += src[f * n_channels_ + c];
                        out[f] = v / (float)n_channels_;
                    }
                } else if (bits_per_sample_ == 16) {
                    const int16_t* src = (const int16_t*)data;
                    for (UINT32 f = 0; f < frames; f++) {
                        float v = 0.0f;
                        for (int c = 0; c < n_channels_; c++) v += (float)src[f * n_channels_ + c];
                        out[f] = v / ((float)n_channels_ * 32768.0f);
                    }
                } else if (bits_per_sample_ == 24) {
                    const uint8_t* src = (const uint8_t*)data;
                    const int bps = n_channels_ * 3;
                    for (UINT32 f = 0; f < frames; f++) {
                        float v = 0.0f;
                        for (int c = 0; c < n_channels_; c++) {
                            const uint8_t* p = src + f * bps + c * 3;
                            int32_t s = (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16));
                            if (s & 0x800000) s |= ~0xFFFFFF; // 符号扩展
                            v += (float)s;
                        }
                        out[f] = v / ((float)n_channels_ * 8388608.0f);
                    }
                } else {
                    // 未知格式：跳过
                    capture_client_->ReleaseBuffer(frames);
                    continue;
                }

                // 增益补偿
                if (cfg_.boost_db != 0.0f) {
                    const float g = std::pow(10.0f, cfg_.boost_db / 20.0f);
                    for (size_t i = 0; i < n_out; i++) out[i] *= g;
                }

                const int64_t t_ms = (int64_t)(GetTickCount64());
                if (on_audio) on_audio(out, n_out, t_ms);
            }
            capture_client_->ReleaseBuffer(frames);
        }
    }
}
