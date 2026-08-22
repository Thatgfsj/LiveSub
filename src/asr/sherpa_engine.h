#pragma once
// ASR 引擎：sherpa-onnx 实现（SenseVoice 离线 / 流式 zipformer 双语）
// 通过 LoadLibrary 动态加载 sherpa-onnx-c-api.dll：
//   - DLL 缺失 → init 失败并提示（不影响 llama 引擎照常使用）
//   - 与现有窗口式识别管线完全兼容：transcribe(整窗) 内部
//     SenseVoice 直接解码；zipformer 新建 stream→feed→收尾→取结果
#include "asr_engine.h"

class SherpaEngine : public IAsrEngine {
public:
    SherpaEngine() = default;
    ~SherpaEngine() override;
    SherpaEngine(const SherpaEngine&) = delete;
    SherpaEngine& operator=(const SherpaEngine&) = delete;

    bool init(const AsrEngineParams& p, std::string* err = nullptr) override;
    void free() override;
    AsrEngineResult transcribe(const float* pcm, size_t n_samples) override;

    int sample_rate() const override { return 16000; }
    bool ready() const override { return initialized_; }
    const std::string& last_error() const override { return last_err_; }

private:
    void* dll_ = nullptr;          // sherpa-onnx-c-api.dll 模块句柄
    void* recognizer_ = nullptr;   // OfflineRecognizer* 或 OnlineRecognizer*
    bool online_ = false;          // true=流式 zipformer / false=SenseVoice 离线
    bool initialized_ = false;
    int n_threads_ = 4;
    std::string model_path_, tokens_path_;
    std::string last_err_;

    // 按模型路径判断模式：含 "encoder" → 流式 zipformer；否则按 SenseVoice 离线
    static bool is_online_path(const std::string& model_path);
    void log(const std::string& msg) const;
};
