#include "text_merge.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <set>
#include <string>

// 常见语气填充词（幻觉过滤用）
static const std::set<std::string> kFillers = {
    // 中文单字/叠字
    "\xe5\x91\x83", // 嗯
    "\xe5\x95\x8a", // 啊
    "\xe5\x91\x83\xe5\x91\x83", // 嗯嗯
    "\xe5\x95\x8a\xe5\x95\x8a", // 啊啊
    "\xe5\x91\x83\xe5\x91\x83\xe5\x91\x83", // 嗯嗯嗯
    "\xe5\x95\x8a\xe5\x95\x8a\xe5\x95\x8a", // 啊啊啊
    "\xe5\x91\x83\xe3\x80\x82", // 嗯。
    "\xe5\x95\x8a\xe3\x80\x82", // 啊。
    "\xe5\x95\x8a\xef\xbc\x81", // 啊！
    // 英文填充
    "uh", "um", "ah", "mm", "hmm", "eh",
    "uh huh", "uh-huh", "mhm", "hmm.", "um.",
};

// 统计 UTF-8 字符数
static size_t utf8_len(const std::string& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        i += (c < 0x80) ? 1 : ((c & 0xE0) == 0xC0) ? 2 : ((c & 0xF0) == 0xE0) ? 3 : 4;
        n++;
    }
    return n;
}

// 去除标点：保留汉字（U+4E00-U+9FFF）与 ASCII 字母数字，去掉中文/英文标点
static std::string strip_punct(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = (unsigned char)s[i];
        if (c < 0x80) {
            if (isalnum(c)) out += (char)c;
            i++;
        } else if ((c & 0xE0) == 0xE0 && i + 2 < s.size()) {
            if (c >= 0xE4 && c <= 0xE9) out.append(s, i, 3); // 汉字
            i += 3;
        } else if ((c & 0xC0) == 0xC0) {
            i += 2;
        } else {
            i++;
        }
    }
    return out;
}

bool TextMerger::is_filler(const std::string& s) {
    // 去首尾空白
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return true; // 空
    size_t e = s.find_last_not_of(" \t\r\n");
    std::string t = s.substr(b, e - b + 1);

    // 长度 ≤ 2 字符且为语气词 → 幻觉
    if (utf8_len(t) <= 2) {
        std::string lower = t;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        // 去尾部标点再查表
        std::string core = lower;
        while (!core.empty() && (core.back() == '.' || core.back() == '!' ||
                                 core.back() == '?' || (unsigned char)core.back() >= 0x80)) {
            core.pop_back();
        }
        if (kFillers.count(core) > 0 || kFillers.count(lower) > 0) {
            return true;
        }
    }
    return false;
}

size_t TextMerger::common_prefix_len(const std::string& a, const std::string& b) {
    size_t n = 0;
    const size_t m = std::min(a.size(), b.size());
    while (n < m && a[n] == b[n]) n++;
    // 回退到 UTF-8 字符边界
    while (n > 0 && ((unsigned char)a[n] & 0xC0) == 0x80) n--;
    return n;
}

void TextMerger::prune(int64_t /*now_ms*/) {
    // 只做内存上限保护：显示行数由 current() 恒定控制，
    // 不在识别循环里移除历史句（避免历史行突然消失/跳动）
    if ((int)sentences_.size() > max_history_) {
        sentences_.erase(sentences_.begin(), sentences_.begin() + (sentences_.size() - max_history_));
    }
}

std::string TextMerger::update(const std::string& new_text, bool finalize, int64_t now_ms) {
    std::string nt = new_text; // 可变副本（跨句粘连截断会修改）
    if (finalize) {
        confirmed_.clear();
        prev_result_.clear();
        prev_interim_.clear();
        // 定稿：幻觉输出不进入历史；与最后一句历史完全相同 → 不重复记录
        if (!nt.empty() && !is_filler(nt) &&
            (sentences_.empty() || sentences_.back().text != nt)) {
            sentences_.push_back({nt, now_ms});
            if ((int)sentences_.size() > max_history_) {
                sentences_.erase(sentences_.begin(), sentences_.begin() + (sentences_.size() - max_history_));
            }
        }
        current_.clear();
        // 返回 current() 而非 nt：定稿后显示"上一句 + 定稿句"两行，
        // 与 interim 期间的行结构一致 → 定稿瞬间不会丢行/蹦字
        return current();
    }

    // 部分结果：幻觉输出不进入字幕
    if (is_filler(nt)) {
        return current();
    }
    // 与刚定稿句完全相同（停顿后窗口重复识别）→ 不显示，避免两行重复
    if (!sentences_.empty() && nt == sentences_.back().text) {
        return current();
    }
    // Local Agreement（whisper_streaming 算法）：
    //   取"上次结果"与"本次结果"的最长公共前缀（去标点比较），
    //   公共前缀就是两次转写都一致的部分 → 确认为稳定文本（永不回退）；
    //   尾部（interim）跟随最新结果更新，渲染层用半透明样式区分
    if (prev_result_.empty()) {
        // 首个结果不确认（LocalAgreement-2：两次一致才确认）：
        // 首窗（约 1s 短音频）识别不稳定，若整体确认会污染前缀，
        // 导致后续窗口内容重排时整句变化（蹦字）。首窗整体半透明，
        // 第二次结果与它的一致前缀才被确认。
        confirmed_.clear();
    } else {
        // 去标点公共前缀：判断两次结果是否同一句（标点差异不影响）
        const size_t cp = common_prefix_len(strip_punct(prev_result_), strip_punct(nt));
        if (cp == 0) {
            confirmed_ = nt;  // 完全不同 → 新句开始：确认重置
            prev_interim_.clear();
        } else {
            // 同一句：确认部分按字节级公共前缀只增不减（不回退）
            const size_t byte_cp = common_prefix_len(prev_result_, nt);
            if (byte_cp > confirmed_.size()) {
                confirmed_ = nt.substr(0, byte_cp);
            }
        }
    }
    prev_result_ = nt;
    // 显示 = confirmed（已确认，稳定不回退）+ interim（未确认尾部）
    // interim 只增不减：上次尾部与本次尾部取较长者（完整打字机效果）
    size_t cf = 0;
    while (cf < confirmed_.size() && cf < nt.size() && confirmed_[cf] == nt[cf]) {
        cf++;
    }
    while (cf > 0 && cf < nt.size() && ((unsigned char)nt[cf] & 0xC0) == 0x80) {
        cf--; // 回退到 UTF-8 字符边界
    }
    const std::string interim_new = nt.substr(cf);
    const std::string interim = (interim_new.size() >= prev_interim_.size())
                                    ? interim_new : prev_interim_;
    current_ = confirmed_ + interim;
    prev_interim_ = interim;
    return current();
}

std::string TextMerger::current() const {
    // 恒定显示结构（无补丁逻辑）：
    //   最近 (max_lines_-1) 句定稿 + 当前句（interim 打字机）
    //   说话时 2 行（上一句 + 当前句），停顿时 1 行（上一句）——行结构稳定不跳变
    const size_t keep = (size_t)std::max(1, max_lines_ - 1);
    std::string s;
    const size_t from = sentences_.size() > keep ? sentences_.size() - keep : 0;
    for (size_t i = from; i < sentences_.size(); i++) {
        if (!s.empty()) s += "\n";
        s += sentences_[i].text;
    }
    if (!current_.empty()) {
        if (!s.empty()) s += "\n";
        s += current_;
    }
    return s;
}

void TextMerger::clear() {
    sentences_.clear();
    current_.clear();
    confirmed_.clear();
    prev_result_.clear();
    prev_interim_.clear();
}
