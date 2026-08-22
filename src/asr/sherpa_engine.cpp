#include "sherpa_engine.h"

#include <cstdio>
#include <cstring>
#include <chrono>

#include <windows.h>

#include "sherpa-onnx-c-api.h"

using namespace std::chrono;

static int64_t now_ms() {
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------
// sherpa-onnx-c-api.dll 动态加载
//   不直接链 import lib：DLL 缺失时进程仍能启动（llama 引擎照常可用），
//   只有用户选 sherpa 模型且 DLL 不在时才提示
// ---------------------------------------------------------------------------
namespace {

struct SherpaApi {
    // offline（SenseVoice）
    const SherpaOnnxOfflineRecognizer* (*CreateOfflineRecognizer)(
        const SherpaOnnxOfflineRecognizerConfig* config);
    void (*DestroyOfflineRecognizer)(const SherpaOnnxOfflineRecognizer* recognizer);
    const SherpaOnnxOfflineStream* (*CreateOfflineStream)(
        const SherpaOnnxOfflineRecognizer* recognizer);
    void (*DestroyOfflineStream)(const SherpaOnnxOfflineStream* stream);
    void (*AcceptWaveformOffline)(const SherpaOnnxOfflineStream* stream,
                                  int32_t sample_rate, const float* samples,
                                  int32_t n);
    void (*DecodeOfflineStream)(const SherpaOnnxOfflineRecognizer* recognizer,
                                const SherpaOnnxOfflineStream* stream);
    const SherpaOnnxOfflineRecognizerResult* (*GetOfflineStreamResult)(
        const SherpaOnnxOfflineStream* stream);
    void (*DestroyOfflineRecognizerResult)(
        const SherpaOnnxOfflineRecognizerResult* r);
    // online（流式 zipformer）
    const SherpaOnnxOnlineRecognizer* (*CreateOnlineRecognizer)(
        const SherpaOnnxOnlineRecognizerConfig* config);
    void (*DestroyOnlineRecognizer)(const SherpaOnnxOnlineRecognizer* recognizer);
    const SherpaOnnxOnlineStream* (*CreateOnlineStream)(
        const SherpaOnnxOnlineRecognizer* recognizer);
    void (*DestroyOnlineStream)(const SherpaOnnxOnlineStream* stream);
    void (*OnlineStreamAcceptWaveform)(const SherpaOnnxOnlineStream* stream,
                                       int32_t sample_rate, const float* samples,
                                       int32_t n);
    int32_t (*IsOnlineStreamReady)(const SherpaOnnxOnlineRecognizer* recognizer,
                                   const SherpaOnnxOnlineStream* stream);
    void (*DecodeOnlineStream)(const SherpaOnnxOnlineRecognizer* recognizer,
                               const SherpaOnnxOnlineStream* stream);
    void (*OnlineStreamInputFinished)(const SherpaOnnxOnlineStream* stream);
    const SherpaOnnxOnlineRecognizerResult* (*GetOnlineStreamResult)(
        const SherpaOnnxOnlineRecognizer* recognizer,
        const SherpaOnnxOnlineStream* stream);
    void (*DestroyOnlineRecognizerResult)(
        const SherpaOnnxOnlineRecognizerResult* r);
};

template <typename T>
static T load_func(HMODULE dll, const char* name, std::string& missing) {
    FARPROC f = GetProcAddress(dll, name);
    if (!f) missing += std::string(missing.empty() ? "" : ", ") + name;
    return reinterpret_cast<T>(f);
}

} // namespace

// 唯一函数表：init 填充 / transcribe·free 使用
// （注意不能在各函数里各自写 static——那是多个独立实例）
static SherpaApi g_api;

bool SherpaEngine::is_online_path(const std::string& model_path) {
    // 流式 zipformer 主文件名为 encoder-epoch99-...onnx；SenseVoice 为 model.int8.onnx
    return model_path.find("encoder") != std::string::npos;
}

void SherpaEngine::log(const std::string& msg) const {
    fprintf(stderr, "[sherpa] %s\n", msg.c_str());
}

SherpaEngine::~SherpaEngine() {
    free();
}

bool SherpaEngine::init(const AsrEngineParams& p, std::string* err) {
    free();
    last_err_.clear();
    model_path_ = p.model_path;
    tokens_path_ = p.aux_path;
    n_threads_ = p.n_threads > 0 ? p.n_threads : 4;
    online_ = is_online_path(model_path_);

    HMODULE dll = LoadLibraryW(L"sherpa-onnx-c-api.dll");
    if (!dll) {
        last_err_ = "缺少 sherpa-onnx-c-api.dll（程序目录）";
        if (err) *err = last_err_;
        return false;
    }
    dll_ = dll;

    SherpaApi& api = g_api;
    api = SherpaApi{}; // 重装 DLL 后重置函数表
    std::string missing;
    api.CreateOfflineRecognizer = load_func<decltype(api.CreateOfflineRecognizer)>(dll, "SherpaOnnxCreateOfflineRecognizer", missing);
    api.DestroyOfflineRecognizer = load_func<decltype(api.DestroyOfflineRecognizer)>(dll, "SherpaOnnxDestroyOfflineRecognizer", missing);
    api.CreateOfflineStream = load_func<decltype(api.CreateOfflineStream)>(dll, "SherpaOnnxCreateOfflineStream", missing);
    api.DestroyOfflineStream = load_func<decltype(api.DestroyOfflineStream)>(dll, "SherpaOnnxDestroyOfflineStream", missing);
    api.AcceptWaveformOffline = load_func<decltype(api.AcceptWaveformOffline)>(dll, "SherpaOnnxAcceptWaveformOffline", missing);
    api.DecodeOfflineStream = load_func<decltype(api.DecodeOfflineStream)>(dll, "SherpaOnnxDecodeOfflineStream", missing);
    api.GetOfflineStreamResult = load_func<decltype(api.GetOfflineStreamResult)>(dll, "SherpaOnnxGetOfflineStreamResult", missing);
    api.DestroyOfflineRecognizerResult = load_func<decltype(api.DestroyOfflineRecognizerResult)>(dll, "SherpaOnnxDestroyOfflineRecognizerResult", missing);
    if (online_) {
        api.CreateOnlineRecognizer = load_func<decltype(api.CreateOnlineRecognizer)>(dll, "SherpaOnnxCreateOnlineRecognizer", missing);
        api.DestroyOnlineRecognizer = load_func<decltype(api.DestroyOnlineRecognizer)>(dll, "SherpaOnnxDestroyOnlineRecognizer", missing);
        api.CreateOnlineStream = load_func<decltype(api.CreateOnlineStream)>(dll, "SherpaOnnxCreateOnlineStream", missing);
        api.DestroyOnlineStream = load_func<decltype(api.DestroyOnlineStream)>(dll, "SherpaOnnxDestroyOnlineStream", missing);
        api.OnlineStreamAcceptWaveform = load_func<decltype(api.OnlineStreamAcceptWaveform)>(dll, "SherpaOnnxOnlineStreamAcceptWaveform", missing);
        api.IsOnlineStreamReady = load_func<decltype(api.IsOnlineStreamReady)>(dll, "SherpaOnnxIsOnlineStreamReady", missing);
        api.DecodeOnlineStream = load_func<decltype(api.DecodeOnlineStream)>(dll, "SherpaOnnxDecodeOnlineStream", missing);
        api.OnlineStreamInputFinished = load_func<decltype(api.OnlineStreamInputFinished)>(dll, "SherpaOnnxOnlineStreamInputFinished", missing);
        api.GetOnlineStreamResult = load_func<decltype(api.GetOnlineStreamResult)>(dll, "SherpaOnnxGetOnlineStreamResult", missing);
        api.DestroyOnlineRecognizerResult = load_func<decltype(api.DestroyOnlineRecognizerResult)>(dll, "SherpaOnnxDestroyOnlineRecognizerResult", missing);
    }
    if (!missing.empty()) {
        last_err_ = "sherpa-onnx-c-api.dll 缺少导出: " + missing;
        if (err) *err = last_err_;
        return false;
    }

    if (online_) {
        // 流式 zipformer 双语（transducer 三件套）
        SherpaOnnxOnlineRecognizerConfig c;
        memset(&c, 0, sizeof(c));
        c.feat_config.sample_rate = 16000;
        c.model_config.transducer.encoder = model_path_.c_str();
        // encoder-xxx.onnx → decoder-xxx.onnx / joiner-xxx.onnx（同目录同名前缀替换）
        auto sibling = [&](const char* from, const char* to) {
            std::string s = model_path_;
            const size_t pos = s.find(from);
            return (pos == std::string::npos) ? s : s.replace(pos, strlen(from), to);
        };
        std::string decoder_path, joiner_path; // config 仅 init 期使用
        decoder_path = sibling("encoder", "decoder");
        joiner_path  = sibling("encoder", "joiner");
        c.model_config.transducer.decoder = decoder_path.c_str();
        c.model_config.transducer.joiner  = joiner_path.c_str();
        c.model_config.tokens       = tokens_path_.c_str();
        c.model_config.num_threads  = n_threads_;
        c.model_config.provider     = "cpu";
        c.decoding_method           = "greedy_search";
        c.enable_endpoint           = 0; // 端点检测由上层 VAD 负责

        recognizer_ = (void*)api.CreateOnlineRecognizer(&c);
    } else {
        // SenseVoice 离线（中英日韩粤，int8）
        SherpaOnnxOfflineRecognizerConfig c;
        memset(&c, 0, sizeof(c));
        c.feat_config.sample_rate = 16000;
        c.model_config.tokens      = tokens_path_.c_str();
        c.model_config.num_threads = n_threads_;
        c.model_config.provider    = "cpu";
        c.model_config.sense_voice.model     = model_path_.c_str();
        c.model_config.sense_voice.language  = ""; // 自动检测
        c.model_config.sense_voice.use_itn   = 1;

        recognizer_ = (void*)api.CreateOfflineRecognizer(&c);
    }
    if (!recognizer_) {
        last_err_ = online_ ? "创建流式识别器失败（检查模型文件）"
                            : "创建 SenseVoice 识别器失败（检查模型文件）";
        if (err) *err = last_err_;
        return false;
    }
    initialized_ = true;
    log(online_ ? "流式 zipformer 加载完成" : "SenseVoice 加载完成");
    return true;
}

void SherpaEngine::free() {
    SherpaApi& api = g_api;
    if (recognizer_) {
        if (online_ && api.DestroyOnlineRecognizer)
            api.DestroyOnlineRecognizer((const SherpaOnnxOnlineRecognizer*)recognizer_);
        if (!online_ && api.DestroyOfflineRecognizer)
            api.DestroyOfflineRecognizer((const SherpaOnnxOfflineRecognizer*)recognizer_);
        recognizer_ = nullptr;
    }
    initialized_ = false;
    if (dll_) {
        FreeLibrary((HMODULE)dll_);
        dll_ = nullptr;
    }
}

AsrEngineResult SherpaEngine::transcribe(const float* pcm, size_t n_samples) {
    AsrEngineResult res;
    if (!initialized_ || !recognizer_ || n_samples == 0) return res;
    const SherpaApi& api = g_api;
    const int64_t t0 = now_ms();

    if (!online_) {
        // SenseVoice：整窗离线解码
        const SherpaOnnxOfflineStream* stream = api.CreateOfflineStream(
            (const SherpaOnnxOfflineRecognizer*)recognizer_);
        if (!stream) { last_err_ = "创建解码流失败"; return res; }
        api.AcceptWaveformOffline(stream, 16000, pcm, (int32_t)n_samples);
        api.DecodeOfflineStream((const SherpaOnnxOfflineRecognizer*)recognizer_, stream);
        const SherpaOnnxOfflineRecognizerResult* r = api.GetOfflineStreamResult(stream);
        if (r && r->text) res.text = r->text;
        if (r) api.DestroyOfflineRecognizerResult(r);
        api.DestroyOfflineStream(stream);
    } else {
        // 流式 zipformer：新建 stream → feed 整窗 → 收尾 → 解码到吐完 → 取文本
        // （窗口独立、无跨窗状态，与上层 LocalAgreement 合并逻辑完全兼容）
        const SherpaOnnxOnlineStream* stream = api.CreateOnlineStream(
            (const SherpaOnnxOnlineRecognizer*)recognizer_);
        if (!stream) { last_err_ = "创建解码流失败"; return res; }
        api.OnlineStreamAcceptWaveform(stream, 16000, pcm, (int32_t)n_samples);
        api.OnlineStreamInputFinished(stream);
        while (api.IsOnlineStreamReady((const SherpaOnnxOnlineRecognizer*)recognizer_, stream)) {
            api.DecodeOnlineStream((const SherpaOnnxOnlineRecognizer*)recognizer_, stream);
        }
        const SherpaOnnxOnlineRecognizerResult* r =
            api.GetOnlineStreamResult((const SherpaOnnxOnlineRecognizer*)recognizer_, stream);
        if (r && r->text) res.text = r->text;
        if (r) api.DestroyOnlineRecognizerResult(r);
        api.DestroyOnlineStream(stream);
    }

    res.decode_ms = now_ms() - t0;
    res.ok = true;
    // 去掉首尾空白（模型偶带空格）
    size_t b = res.text.find_first_not_of(" \t\r\n");
    size_t e = res.text.find_last_not_of(" \t\r\n");
    res.text = (b == std::string::npos) ? "" : res.text.substr(b, e - b + 1);
    return res;
}
