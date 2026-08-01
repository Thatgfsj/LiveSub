#pragma once
// ASR 引擎：封装 llama.cpp + libmtmd，将 16kHz 单声道 PCM 转写为文本
// 每个 transcribe() 调用处理一个音频窗口（同步阻塞，供 ASR 线程串行调用）
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

struct llama_model;
struct llama_context;
struct llama_sampler;
struct mtmd_context;

class AsrEngine {
public:
    struct Params {
        std::string model_path;      // 主模型 GGUF
        std::string mmproj_path;     // 音频编码器 GGUF
        int n_threads     = 18;      // CPU 线程
        int gpu_layers    = 0;       // 0=纯 CPU；>0 = 主模型 GPU 层数（Vulkan）
        int n_batch       = 256;
        int n_ctx         = 2048;    // 上下文（音频嵌入 token + 文本）
        int max_new_tokens = 128;
        std::string prompt = "Transcribe the audio.";
        int verbosity     = 1;
    };

    struct Result {
        bool ok = false;
        std::string text;        // 转写文本（已去除 language/<asr_text> 前缀）
        std::string raw;         // 原始模型输出
        std::string language;    // 检测语言（如 "Chinese"），未检测到则空
        int64_t encode_ms = 0;   // 音频编码耗时
        int64_t decode_ms = 0;   // 文本解码耗时
    };

    AsrEngine() = default;
    ~AsrEngine();
    AsrEngine(const AsrEngine&) = delete;
    AsrEngine& operator=(const AsrEngine&) = delete;

    // 加载模型，失败返回 false 并填充 err
    bool init(const Params& p, std::string* err = nullptr);
    void free();

    // 同步转写一段 16kHz 单声道 float PCM
    Result transcribe(const float* pcm, size_t n_samples);

    int sample_rate() const { return sr_; }
    bool ready() const { return mtmd_ != nullptr; }
    const std::string& last_error() const { return last_err_; }

private:
    Params p_;
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
    static void parse_output(const std::string& raw, Result& r);
    void log(const std::string& msg) const;
};
