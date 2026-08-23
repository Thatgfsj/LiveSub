#pragma once
// ASR 引擎：统一接口 IAsrEngine + llama.cpp/mtmd 实现（AsrEngine）
//   - AsrEngine：llama.cpp + libmtmd（Qwen3-ASR GGUF）
//   - SherpaEngine：sherpa-onnx（SenseVoice / 流式 zipformer，见 sherpa_engine.h）
// App 按配置的 model_size 用工厂函数创建，ASR 线程串行调用 transcribe()
// 每个 transcribe() 处理一个音频窗口（同步阻塞），窗口间无状态依赖
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

struct llama_model;
struct llama_context;
struct llama_sampler;
struct mtmd_context;

// 两种引擎通用的初始化参数（各引擎忽略用不到的字段）
struct AsrEngineParams {
    std::string model_path;   // 主模型（llama: 主模型 GGUF / sherpa: ONNX）
    std::string aux_path;     // 辅助文件（llama: mmproj GGUF / sherpa: tokens.txt）
    int n_threads      = 18;  // CPU 线程
    int gpu_layers     = 0;   // llama 专用：0=纯 CPU；>0 = GPU 层数（Vulkan）
    int n_batch        = 256; // llama 专用
    int n_ctx          = 2048;// llama 专用：上下文（音频嵌入 token + 文本）
    int max_new_tokens = 128; // llama 专用
    std::string prompt = "Transcribe the audio."; // llama 专用
    int verbosity      = 1;
};

struct AsrEngineResult {
    bool ok = false;
    std::string text;        // 转写文本（纯文本）
    std::string raw;         // 原始输出（llama 专用）
    std::string language;    // 检测语言（如 "Chinese"），未检测到则空
    int64_t encode_ms = 0;   // 音频编码耗时
    int64_t decode_ms = 0;   // 文本解码耗时
};

class IAsrEngine {
public:
    // 引擎日志出口：GUI 程序 stderr 不可见 → 由 App 注入（写 livesub.log）
    // 未设置时引擎自行 fprintf(stderr)
    static void (*logger)(const char* msg);

    virtual ~IAsrEngine() = default;
    // 加载模型，失败返回 false 并填充 err
    virtual bool init(const AsrEngineParams& p, std::string* err = nullptr) = 0;
    virtual void free() = 0;
    // 同步转写一段 16kHz 单声道 float PCM
    virtual AsrEngineResult transcribe(const float* pcm, size_t n_samples) = 0;
    virtual int sample_rate() const = 0;
    virtual bool ready() const = 0;
    virtual const std::string& last_error() const = 0;
};

// llama.cpp + libmtmd 实现（Qwen3-ASR）
class AsrEngine : public IAsrEngine {
public:
    AsrEngine() = default;
    ~AsrEngine();
    AsrEngine(const AsrEngine&) = delete;
    AsrEngine& operator=(const AsrEngine&) = delete;

    bool init(const AsrEngineParams& p, std::string* err = nullptr) override;
    void free() override;
    AsrEngineResult transcribe(const float* pcm, size_t n_samples) override;

    int sample_rate() const override { return sr_; }
    bool ready() const override { return mtmd_ != nullptr; }
    const std::string& last_error() const override { return last_err_; }

private:
    AsrEngineParams p_;
    llama_model*   model_ = nullptr;
    llama_context* ctx_   = nullptr;
    llama_sampler* smpl_  = nullptr;
    mtmd_context*  mtmd_  = nullptr;
    int sr_ = 16000;
    bool backend_inited_ = false;
    std::string last_err_;
    std::vector<char> piece_buf_;

    std::string build_prompt() const;
    // 解析模型输出：提取 language 与 <asr_text> 后的正文
    static void parse_output(const std::string& raw, AsrEngineResult& r);
    void log(const std::string& msg) const;
};
