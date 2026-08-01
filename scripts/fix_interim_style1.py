# -*- coding: utf-8 -*-
# 1. TextMerger：confirmed_offset()（confirmed 在 current() 输出中的偏移）
p = r'src\asr\text_merge.h'
s = open(p, encoding='utf-8').read()
old = '''    // Local Agreement（whisper_streaming）：当前句已确认部分的字符数
    // （渲染层用它把"已确认"与"未确认尾部"用不同样式显示）
    size_t confirmed_chars() const { return confirmed_.size(); }'''
new = '''    // Local Agreement（whisper_streaming）：当前句已确认部分的字符数
    size_t confirmed_chars() const { return confirmed_.size(); }

    // confirmed 部分在 current() 输出字符串中的偏移（渲染层用于样式区分）
    size_t confirmed_offset() const {
        const std::string cur = current();
        if (current_.empty()) return cur.size();
        return cur.size() - current_.size() + confirmed_.size();
    }'''
assert old in s, 'h offset'
s = s.replace(old, new)
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('text_merge.h OK')

# 2. app.cpp：set_text 传 confirmed 偏移
p = r'src\app.cpp'
s = open(p, encoding='utf-8').read()
old = '''                window_.set_text(full);
                if (finalize) window_.set_status("");'''
new = '''                window_.set_text(full, merger_.confirmed_offset());
                if (finalize) window_.set_status("");'''
assert old in s, 'set_text call'
s = s.replace(old, new)
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('app.cpp OK')

# 3. subtitle_window.h：set_text 带偏移；interim_brush_
p = r'src\ui\subtitle_window.h'
s = open(p, encoding='utf-8').read()
old = '''    // 线程安全：更新字幕内容（UTF-8）
    void set_text(const std::string& text);
    // 设置状态文本（如"识别中…"），UTF-8
    void set_status(const std::string& status);'''
new = '''    // 线程安全：更新字幕内容（UTF-8）；confirmed_offset 为已确认部分偏移
    // （其前实色显示，其后为未确认 interim 半透明显示）
    void set_text(const std::string& text, size_t confirmed_offset = std::string::npos);
    // 设置状态文本（如"识别中…"），UTF-8
    void set_status(const std::string& status);'''
assert old in s, 'h set_text'
s = s.replace(old, new)
old = '''    ID2D1SolidColorBrush* text_brush_ = nullptr;
    IDWriteFactory* dwrite_factory_ = nullptr;
    IDWriteTextFormat* text_format_ = nullptr;
    IDWriteTextLayout* layout_ = nullptr; // 布局缓存（含行数截断）'''
new = '''    ID2D1SolidColorBrush* text_brush_ = nullptr;
    ID2D1SolidColorBrush* interim_brush_ = nullptr; // interim 半透明样式
    IDWriteFactory* dwrite_factory_ = nullptr;
    IDWriteTextFormat* text_format_ = nullptr;
    IDWriteTextLayout* layout_ = nullptr; // 布局缓存'''
assert old in s, 'h brush'
s = s.replace(old, new)
old = '''    // 文本变化时重建布局（跨线程标记）
    std::atomic<bool> layout_dirty_{true};
    float last_layout_size_ = 0.0f; // 上次布局字号（平滑用，避免缩放跳变）'''
new = '''    // 文本变化时重建布局（跨线程标记）
    std::atomic<bool> layout_dirty_{true};
    float last_layout_size_ = 0.0f; // 上次布局字号（平滑用，避免缩放跳变）
    size_t confirmed_offset_ = std::string::npos; // 已确认部分偏移（interim 样式）'''
assert old in s, 'h offset member'
s = s.replace(old, new)
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('subtitle_window.h OK')
