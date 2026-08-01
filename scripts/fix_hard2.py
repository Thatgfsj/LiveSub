# -*- coding: utf-8 -*-
# 硬性修复3：渲染层移除"砍句"滚动截断；段落底部对齐（最新内容保留）
p = r'src\ui\subtitle_window.cpp'
s = open(p, encoding='utf-8').read()

# 3a. 段落对齐改为 FAR（底部）：超行时裁掉的是旧内容，最新内容保留
old = '''        if (text_format_) {
            text_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            text_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }'''
new = '''        if (text_format_) {
            text_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            // 底部对齐：行数超出时被裁的是顶部旧内容，最新字幕始终可见
            text_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_FAR);
        }'''
assert old in s, 'align'
s = s.replace(old, new)

# 3b. rebuild_layout：移除滚动截断（按行砍句），保留字号自适应
old_start = s.find('// 构建文本布局：')
old_end = s.find('void SubtitleWindow::to_wide')
assert old_start != -1 and old_end != -1 and old_start < old_end

new_block = '''// 构建文本布局：
//   1. 长单句超行时逐级缩小字号（避免"第二行孤字"）
//   2. 不做"按行砍句"截断——句子完整性由 TextMerger 保证（只输出最近 N 句），
//      超行时靠底部对齐让最新内容保留（旧内容自然被裁）
void SubtitleWindow::rebuild_layout(const std::wstring& text, float w, float h) {
    if (layout_) { layout_->Release(); layout_ = nullptr; }
    if (!dwrite_factory_ || !text_format_ || text.empty()) return;

    auto make = [&](const std::wstring& t) -> IDWriteTextLayout* {
        IDWriteTextLayout* l = nullptr;
        if (FAILED(dwrite_factory_->CreateTextLayout(t.c_str(), (UINT32)t.size(),
                                                     text_format_, w, h, &l))) {
            return nullptr;
        }
        return l;
    };

    const UINT32 max = (UINT32)std::max(1, style_.max_lines);
    const float min_size = style_.font_size * 0.5f;

    IDWriteTextLayout* l = make(text);
    if (!l) return;

    // 1. 字号自适应：仅对【超长的单句】（不含显式换行）逐级缩小字号
    const bool multi_line_text = text.find(L'\\n') != std::wstring::npos;
    float size = style_.font_size;
    if (!multi_line_text) {
        for (int attempt = 0; attempt < 14; attempt++) {
            DWRITE_TEXT_RANGE range = {0, (UINT32)text.size()};
            l->SetFontSize(size, range);
            UINT32 lines = 0;
            l->GetLineMetrics(nullptr, 0, &lines);
            if (lines <= max || size <= min_size) break;
            size *= 0.92f;
        }
        last_layout_size_ = size;
    }
    layout_ = l;
}

'''
s = s[:old_start] + new_block + s[old_end:]
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('subtitle_window.cpp OK')
