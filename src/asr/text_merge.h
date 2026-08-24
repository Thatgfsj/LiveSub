#pragma once
// 流式文本合并：将相邻窗口的识别结果稳定合并为字幕文本
// 策略：
//   - 幻觉过滤：短语气词输出（"嗯。"等）不进入字幕
//   - 保留"已稳定"前缀，尾部不稳定内容持续更新
//   - 前缀不一致时以最新识别为准（旧文本可能是幻觉/过期内容）
//   - 句末（静音提交）时定稿当前句
#include <cstdint>
#include <string>
#include <vector>

class TextMerger {
public:
    // 新结果到达：update(new_text, finalize, now_ms)
    //   finalize=true  → 当前句定稿，合并进历史，返回定稿句
    //   finalize=false → 更新当前句（部分结果），返回当前完整字幕文本
    //   now_ms 用于记录句子时间（滚动缓冲用），传 0 表示不记录
    std::string update(const std::string& new_text, bool finalize, int64_t now_ms = 0);

    // 真流式引擎专用（流式 zipformer）：结果前缀稳定、只追加，
    // 直接作为当前句显示，不走 Local Agreement（那是增长窗口重解模式的逻辑）
    std::string update_streaming(const std::string& full_text, bool finalize, int64_t now_ms = 0);

    // Local Agreement（whisper_streaming）：当前句已确认部分的字符数
    size_t confirmed_chars() const { return confirmed_.size(); }

    // confirmed 部分在 current() 输出字符串中的偏移（渲染层用于样式区分）
    size_t confirmed_offset() const {
        const std::string cur = current();
        if (current_.empty()) return cur.size();
        return cur.size() - current_.size() + confirmed_.size();
    }

    // 滚动缓冲修剪：显示超过 2 行（出现第三行缓冲）时，
    // 最旧句子停留 1 秒后移除（"第三行出现时第一行 1s 后消失"）
    void prune(int64_t now_ms);

    // 当前完整字幕文本（历史 + 当前句）
    std::string current() const;

    // 历史句（最多 keep 句，用于多行显示，最新在前）
    std::vector<std::string> history(int keep = 2) const;

    void clear();
    // 字幕最多显示几行（历史句 + 当前句），超出只保留最近 N 行
    void set_max_lines(int n) { max_lines_ = std::max(1, n); }

    // 判断是否为幻觉/填充输出（短语气词），供外部调用
    static bool is_filler(const std::string& s);

private:
    struct Sent {
        std::string text;
        int64_t ts = 0; // 定稿时间（毫秒，滚动缓冲用）
    };
    std::vector<Sent> sentences_; // 已定稿句子（旧→新）
    std::string current_;         // 当前句（部分结果：confirmed + interim）
    std::string confirmed_;       // 当前句已确认部分（Local Agreement，永不回退）
    std::string prev_result_;     // 上次部分结果（用于公共前缀计算）
    std::string prev_interim_;    // 上次未确认尾部（interim 只增不减）
    int max_history_ = 8;
    int max_lines_ = 2;           // 固定 2 行：上一句 + 当前句

    // 最长公共前缀长度（UTF-8 安全：只在字符边界切割）
    static size_t common_prefix_len(const std::string& a, const std::string& b);
};
