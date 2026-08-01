# -*- coding: utf-8 -*-
# subtitle_window.cpp：interim 半透明样式
p = r'src\ui\subtitle_window.cpp'
s = open(p, encoding='utf-8').read()

# 1. release_d2d 释放 interim_brush_
old = '''    if (layout_)      { layout_->Release();      layout_      = nullptr; }
    if (text_format_) { text_format_->Release(); text_format_ = nullptr; }
    if (text_brush_)  { text_brush_->Release();  text_brush_  = nullptr; }
    if (bg_brush_)    { bg_brush_->Release();    bg_brush_    = nullptr; }
    if (target_)      { target_->Release();      target_      = nullptr; }'''
new = '''    if (layout_)         { layout_->Release();         layout_         = nullptr; }
    if (text_format_)    { text_format_->Release();    text_format_    = nullptr; }
    if (text_brush_)     { text_brush_->Release();     text_brush_     = nullptr; }
    if (interim_brush_)  { interim_brush_->Release();  interim_brush_  = nullptr; }
    if (bg_brush_)       { bg_brush_->Release();       bg_brush_       = nullptr; }
    if (target_)         { target_->Release();         target_         = nullptr; }'''
assert old in s, 'release'
s = s.replace(old, new)

# 2. apply_style 创建 interim_brush_（文字色半透明版）
old = '''            target_->CreateSolidColorBrush(
                D2D1::ColorF((style_.font_color >> 16 & 0xFF) / 255.0f,
                             (style_.font_color >> 8 & 0xFF) / 255.0f,
                             (style_.font_color & 0xFF) / 255.0f,
                             (style_.font_color >> 24 & 0xFF) / 255.0f),
                &text_brush_);'''
new = '''            target_->CreateSolidColorBrush(
                D2D1::ColorF((style_.font_color >> 16 & 0xFF) / 255.0f,
                             (style_.font_color >> 8 & 0xFF) / 255.0f,
                             (style_.font_color & 0xFF) / 255.0f,
                             (style_.font_color >> 24 & 0xFF) / 255.0f),
                &text_brush_);
            // interim（未确认尾部）：文字色 45% 不透明
            target_->CreateSolidColorBrush(
                D2D1::ColorF((style_.font_color >> 16 & 0xFF) / 255.0f,
                             (style_.font_color >> 8 & 0xFF) / 255.0f,
                             (style_.font_color & 0xFF) / 255.0f,
                             ((style_.font_color >> 24 & 0xFF) / 255.0f) * 0.45f),
                &interim_brush_);'''
assert old in s, 'interim brush'
s = s.replace(old, new)

# 3. set_text 存偏移
old = '''void SubtitleWindow::set_text(const std::string& text) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        content_ = to_wide(text);
    }
    layout_dirty_ = true;
}'''
new = '''void SubtitleWindow::set_text(const std::string& text, size_t confirmed_offset) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        content_ = to_wide(text);
    }
    confirmed_offset_ = confirmed_offset;
    layout_dirty_ = true;
}'''
assert old in s, 'set_text'
s = s.replace(old, new)

# 4. rebuild_layout 后：设置 interim 范围样式
old = '''    const bool multi_line_text = text.find(L'\\n') != std::wstring::npos;
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
    }
    layout_ = l;
}'''
new = '''    const bool multi_line_text = text.find(L'\\n') != std::wstring::npos;
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
    }

    // interim（未确认尾部）半透明样式：confirmed 偏移之后的部分
    if (interim_brush_ && confirmed_offset_ != std::string::npos &&
        confirmed_offset_ < text.size()) {
        DWRITE_TEXT_RANGE range = {(UINT32)confirmed_offset_,
                                   (UINT32)(text.size() - confirmed_offset_)};
        l->SetDrawingEffect(interim_brush_, range);
    }
    layout_ = l;
}'''
assert old in s, 'interim range'
s = s.replace(old, new)

open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('subtitle_window.cpp OK')
