#include "asr_engine.h"

#include <cstdio>
#include <chrono>
#include <algorithm>

#include "llama.h"
#include "mtmd.h"
#include "mtmd-helper.h"

using namespace std::chrono;

// 引擎日志出口（App 注入；未注入时引擎直接 fprintf(stderr)，GUI 模式不可见）
void (*IAsrEngine::logger)(const char* msg) = nullptr;

// ---------------------------------------------------------------------------
// llama.cpp 日志回调
// ---------------------------------------------------------------------------
static void llama_log_cb(enum ggml_log_level level, const char* text, void* /*user_data*/) {
    // 仅转发错误与警告
    if (level >= GGML_LOG_LEVEL_WARN) {
        fprintf(stderr, "%s", text);
    }
}

AsrEngine::~AsrEngine() {
    free();
}

static int64_t now_ms() {
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool AsrEngine::init(const AsrEngineParams& p, std::string* err) {
    free();
    p_ = p;

    llama_log_set(llama_log_cb, nullptr);
    llama_backend_init();

    // 1. 主模型
    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = p_.gpu_layers;

    model_ = llama_load_model_from_file(p_.model_path.c_str(), mparams);
    if (!model_) {
        last_err_ = "无法加载主模型: " + p_.model_path;
        if (err) *err = last_err_;
        return false;
    }

    // 2. 解码上下文
    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx     = p_.n_ctx;
    cparams.n_batch   = p_.n_batch;
    cparams.n_threads = p_.n_threads;
    cparams.n_threads_batch = p_.n_threads;
    cparams.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_AUTO;

    ctx_ = llama_new_context_with_model(model_, cparams);
    if (!ctx_) {
        last_err_ = "创建解码上下文失败";
        if (err) *err = last_err_;
        return false;
    }

    // 3. 采样器（greedy，ASR 稳定输出）
    smpl_ = llama_sampler_init_greedy();
    if (!smpl_) {
        last_err_ = "创建采样器失败";
        if (err) *err = last_err_;
        return false;
    }

    // 4. 多模态（音频编码器）上下文
    mtmd_context_params mctx_params = mtmd_context_params_default();
    mctx_params.use_gpu        = p_.gpu_layers > 0;
    mctx_params.n_threads      = p_.n_threads;
    mctx_params.print_timings  = false;
    mctx_params.warmup         = true;

    mtmd_ = mtmd_init_from_file(p_.aux_path.c_str(), model_, mctx_params);
    if (!mtmd_) {
        last_err_ = "无法加载音频编码器 (mmproj): " + p_.aux_path;
        if (err) *err = last_err_;
        return false;
    }
    if (!mtmd_support_audio(mtmd_)) {
        last_err_ = "mmproj 不支持音频输入";
        if (err) *err = last_err_;
        return false;
    }
    sr_ = mtmd_get_audio_sample_rate(mtmd_);
    if (sr_ <= 0) sr_ = 16000;

    piece_buf_.resize(16 * 1024);
    backend_inited_ = true;
    log("模型加载完成，音频采样率 " + std::to_string(sr_) + " Hz");
    return true;
}

void AsrEngine::free() {
    if (mtmd_) { mtmd_free(mtmd_); mtmd_ = nullptr; }
    if (smpl_) { llama_sampler_free(smpl_); smpl_ = nullptr; }
    if (ctx_)  { llama_free(ctx_);  ctx_  = nullptr; }
    if (model_) { llama_free_model(model_); model_ = nullptr; }
    if (backend_inited_) {
        llama_backend_free();
        backend_inited_ = false;
    }
}

std::string AsrEngine::build_prompt() const {
    // Qwen3-ASR 期望 chatml 格式；<__media__> 由 mtmd 替换为音频嵌入
    // （自动包裹 <|audio_start|> / <|audio_end|>）
    return "<|im_start|>user\n<__media__>" + p_.prompt + "<|im_end|>\n<|im_start|>assistant\n";
}

void AsrEngine::log(const std::string& msg) const {
    if (p_.verbosity >= 1) {
        fprintf(stderr, "[asr] %s\n", msg.c_str());
    }
}

AsrEngineResult AsrEngine::transcribe(const float* pcm, size_t n_samples) {
    AsrEngineResult res;
    const int64_t t0 = now_ms();

    if (!mtmd_ || n_samples == 0) return res;

    // 清空 KV 缓存：每次 transcribe 独立解码（窗口之间无依赖）
    llama_memory_clear(llama_get_memory(ctx_), true);

    // 1. 音频 → bitmap
    mtmd_bitmap* bitmap = mtmd_bitmap_init_from_audio(n_samples, pcm);
    if (!bitmap) { last_err_ = "创建音频位图失败"; return res; }

    // 2. tokenize（prompt + 音频）
    const std::string prompt = build_prompt();
    mtmd_input_text text;
    text.text          = prompt.data();
    text.text_len      = prompt.size();
    text.add_special   = true;
    text.parse_special = true;

    mtmd_input_chunks* chunks = mtmd_input_chunks_init();
    const mtmd_bitmap* bitmaps[1] = { bitmap };
    const int32_t tok_res = mtmd_tokenize(mtmd_, chunks, &text, bitmaps, 1);
    mtmd_bitmap_free(bitmap);
    if (tok_res != 0) {
        mtmd_input_chunks_free(chunks);
        last_err_ = "tokenize 失败, res=" + std::to_string(tok_res);
        return res;
    }

    // 3. 逐 chunk 编码/解码（文本 chunk 与音频 chunk 交替）
    const size_t n_chunks = mtmd_input_chunks_size(chunks);
    if (p_.verbosity >= 2) {
        fprintf(stderr, "[asr] prompt=%s\n[asr] chunks=%zu: ", prompt.c_str(), n_chunks);
        for (size_t i = 0; i < n_chunks; i++) {
            const mtmd_input_chunk* c = mtmd_input_chunks_get(chunks, i);
            fprintf(stderr, "[%zu:%s:%zu]", i,
                    mtmd_input_chunk_get_type(c) == MTMD_INPUT_CHUNK_TYPE_TEXT ? "txt" : "aud",
                    mtmd_input_chunk_get_n_tokens(c));
        }
        fprintf(stderr, "\n");
    }
    llama_pos n_past = 0;
    bool ok = true;
    for (size_t i = 0; i < n_chunks; i++) {
        const mtmd_input_chunk* chunk = mtmd_input_chunks_get(chunks, i);
        const enum mtmd_input_chunk_type type = mtmd_input_chunk_get_type(chunk);
        const bool logits_last = (i == n_chunks - 1);

        if (type == MTMD_INPUT_CHUNK_TYPE_TEXT) {
            llama_pos new_n_past = n_past;
            const int32_t r = mtmd_helper_eval_chunk_single(mtmd_, ctx_, chunk, n_past, 0,
                                                            p_.n_batch, logits_last, &new_n_past);
            if (r != 0) { ok = false; break; }
            n_past = new_n_past;
        } else if (type == MTMD_INPUT_CHUNK_TYPE_AUDIO) {
            const int64_t t_enc = now_ms();
            // 用 batch API（与 mtmd-cli 一致的已验证路径）
            mtmd_batch* mbatch = mtmd_batch_init(mtmd_);
            const int32_t r_add = mtmd_batch_add_chunk(mbatch, chunk);
            if (r_add != 0) { mtmd_batch_free(mbatch); ok = false; break; }
            const int32_t r_enc = mtmd_batch_encode(mbatch);
            if (r_enc != 0) { mtmd_batch_free(mbatch); ok = false; break; }
            float* embd = mtmd_batch_get_output_embd(mbatch, chunk);
            if (!embd) { mtmd_batch_free(mbatch); ok = false; break; }
            res.encode_ms += now_ms() - t_enc;

            llama_pos new_n_past = n_past;
            const int32_t r_dec = mtmd_helper_decode_image_chunk(mtmd_, ctx_, chunk, embd,
                                                                 n_past, 0, p_.n_batch,
                                                                 &new_n_past, nullptr, nullptr);
            mtmd_batch_free(mbatch);
            if (r_dec != 0) { ok = false; break; }
            n_past = new_n_past;
        } else {
            ok = false; // 不支持的类型（如视频）
            break;
        }
    }

    // 4. 自回归采样
    const int64_t t_dec = now_ms();
    if (ok) {
        for (int i = 0; i < p_.max_new_tokens; i++) {
            const llama_token token = llama_sampler_sample(smpl_, ctx_, -1);
            if (llama_vocab_is_eog(llama_model_get_vocab(model_), token)) {
                break;
            }
            // 追加并解码（手动构建 batch，指定位置与 logits）
            llama_batch batch = llama_batch_init(1, 0, 1);
            batch.n_tokens    = 1;
            batch.token[0]    = token;
            batch.pos[0]      = n_past;
            batch.n_seq_id[0] = 1;
            batch.seq_id[0][0] = 0;
            batch.logits[0]   = true;
            const int32_t dec_ok = llama_decode(ctx_, batch);
            llama_batch_free(batch);
            if (dec_ok != 0) { ok = false; break; }
            n_past += 1;

            // 收集 piece
            const int n = llama_token_to_piece(llama_model_get_vocab(model_), token,
                                               piece_buf_.data(), (int)piece_buf_.size(),
                                               0, true);
            if (n > 0) {
                res.raw.append(piece_buf_.data(), (size_t)n);
            } else if (n < 0) {
                piece_buf_.resize((size_t)(-n));
                const int n2 = llama_token_to_piece(llama_model_get_vocab(model_), token,
                                                    piece_buf_.data(), (int)piece_buf_.size(),
                                                    0, true);
                if (n2 > 0) res.raw.append(piece_buf_.data(), (size_t)n2);
            }
        }
    }
    res.decode_ms = now_ms() - t_dec;
    mtmd_input_chunks_free(chunks);

    res.ok = ok;
    parse_output(res.raw, res);
    return res;
}

void AsrEngine::parse_output(const std::string& raw, AsrEngineResult& r) {
    // 模型输出形如: language Chinese<asr_text>甚至出现交易几乎停滞的情况。
    // 或: <asr_text>文本
    const std::string lang_marker = "language ";
    const std::string text_marker = "<asr_text>";
    size_t p = raw.find(text_marker);
    if (p != std::string::npos) {
        // 提取语言（在文本标记之前）
        size_t lp = raw.find(lang_marker);
        if (lp != std::string::npos && lp < p) {
            size_t le = raw.find_first_of(" \n<", lp + lang_marker.size());
            if (le == std::string::npos || le > p) le = p;
            r.language = raw.substr(lp + lang_marker.size(), le - (lp + lang_marker.size()));
        }
        r.text = raw.substr(p + text_marker.size());
    } else if (raw.find(lang_marker) != std::string::npos) {
        // 只有语言，无文本
        size_t lp = raw.find(lang_marker);
        size_t le = raw.find_first_of(" \n<", lp + lang_marker.size());
        r.language = raw.substr(lp + lang_marker.size(), le - (lp + lang_marker.size()));
        r.text.clear();
    } else {
        r.text = raw;
    }
    // 清理残留的特殊 token（防御性）
    const std::string im_end = "<|im_end|>";
    size_t e = r.text.find(im_end);
    if (e != std::string::npos) r.text.erase(e);
    // 去除首尾空白
    size_t b = r.text.find_first_not_of(" \t\r\n");
    size_t f = r.text.find_last_not_of(" \t\r\n");
    r.text = (b == std::string::npos) ? "" : r.text.substr(b, f - b + 1);
}
