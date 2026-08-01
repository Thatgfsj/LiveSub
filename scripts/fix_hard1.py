# -*- coding: utf-8 -*-
# 硬性修复1：update 移到 r.ok 内（识别失败不清当前句）
p = r'src\app.cpp'
s = open(p, encoding='utf-8').read()

old = '''        // 定稿判定：VAD 静音到达（语音段结束）
        const std::string full = merger_.update(r.text, finalize, now_ms());
        if (finalize) {
            last_finalize_ms_ = now_ms();
        }

        if (r.ok) {
            if (!r.text.empty()) {'''
new = '''        if (r.ok) {
            // 定稿判定：VAD 静音到达（语音段结束）；识别失败不更新合并器
            // （避免失败时清空当前句导致显示丢失）
            const std::string full = merger_.update(r.text, finalize, now_ms());
            if (finalize) {
                last_finalize_ms_ = now_ms();
            }
            if (!r.text.empty()) {'''
assert old in s, 'update move'
s = s.replace(old, new)

# 补上缺失的括号结构：原 r.ok 块以 } else { 结束，现在需要闭合
old = '''            } else if (finalize) {
                // 定稿但无文本（可能只是噪音/气声）
                window_.set_status("未识别到语音（可调低 VAD 门限或靠近麦克风）");
            }
            if (cfg_.log_level >= 1) {
                logf("[asr] %s | enc=%lldms dec=%lldms total=%lldms%s\\n",
                     r.text.empty() ? "(空)" : r.text.c_str(),
                     (long long)r.encode_ms, (long long)r.decode_ms, (long long)cost,
                     finalize ? " [FINAL]" : "");
            }
        } else {'''
new = '''                if (finalize) {
                    // 定稿但无文本（可能只是噪音/气声）
                    window_.set_status("未识别到语音（可调低 VAD 门限或靠近麦克风）");
                }
            }
            if (cfg_.log_level >= 1) {
                logf("[asr] %s | enc=%lldms dec=%lldms total=%lldms%s\\n",
                     r.text.empty() ? "(空)" : r.text.c_str(),
                     (long long)r.encode_ms, (long long)r.decode_ms, (long long)cost,
                     finalize ? " [FINAL]" : "");
            }
        } else {'''
assert old in s, 'restructure'
s = s.replace(old, new)

open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('app.cpp OK')

# 硬性修复4：prune 用 max_lines_（去掉硬编码 2）
p = r'src\asr\text_merge.cpp'
s = open(p, encoding='utf-8').read()
old = '''void TextMerger::prune(int64_t now_ms) {
    // 常态 2 行；超过 2 行（第 3 行缓冲出现）时，最旧句停留 1 秒后移除
    while (!sentences_.empty()) {
        const int total = (int)sentences_.size() + (current_.empty() ? 0 : 1);
        if (total <= 2) break;                       // 未超 2 行 → 保留
        if (now_ms - sentences_.front().ts < 1000) break; // 最旧句未满 1 秒
        sentences_.erase(sentences_.begin());
    }
    // 历史长度上限保护
    if ((int)sentences_.size() > max_history_) {
        sentences_.erase(sentences_.begin(), sentences_.begin() + (sentences_.size() - max_history_));
    }
}'''
new = '''void TextMerger::prune(int64_t now_ms) {
    // 历史句保留数 = max_lines_ - 当前句占位；超出时最旧句停留 1 秒后移除
    const int keep = std::max(1, max_lines_ - (current_.empty() ? 0 : 1));
    while ((int)sentences_.size() > keep) {
        if (now_ms - sentences_.front().ts < 1000) break; // 最旧句未满 1 秒
        sentences_.erase(sentences_.begin());
    }
    // 历史长度上限保护
    if ((int)sentences_.size() > max_history_) {
        sentences_.erase(sentences_.begin(), sentences_.begin() + (sentences_.size() - max_history_));
    }
}'''
assert old in s, 'prune'
s = s.replace(old, new)

# 硬性修复2：current() 只输出最近 max_lines_ 句（句子级滚动，不砍句子）
old = '''    std::string s;
    const size_t keep = std::min(sentences_.size(), (size_t)max_history_);
    for (size_t i = sentences_.size() - keep; i < sentences_.size(); i++) {
        if (!s.empty()) s += "\\n";
        s += sentences_[i].text;
    }
    if (!current_.empty()) {
        if (!s.empty()) s += "\\n";
        s += current_;
    }
    return s;
}'''
new = '''    // 只输出最近 max_lines_ 句（句子级滚动，完整句子不砍断）
    std::string s;
    const size_t keep_sent = std::max(0, max_lines_ - (current_.empty() ? 0 : 1));
    if (keep_sent > 0 && !sentences_.empty()) {
        const size_t from = (sentences_.size() > (size_t)keep_sent) ? sentences_.size() - keep_sent : 0;
        for (size_t i = from; i < sentences_.size(); i++) {
            if (!s.empty()) s += "\\n";
            s += sentences_[i].text;
        }
    }
    if (!current_.empty()) {
        if (!s.empty()) s += "\\n";
        s += current_;
    }
    return s;
}'''
assert old in s, 'current keep'
s = s.replace(old, new)
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('text_merge.cpp OK')
