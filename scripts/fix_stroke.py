# -*- coding: utf-8 -*-
# subtitle_window：字体描边（8 方向偏移绘制黑色描边 + 原位填充，艺术字效果）
p = r'src\ui\subtitle_window.h'
s = open(p, encoding='utf-8').read()
old = '''    ID2D1SolidColorBrush* text_brush_ = nullptr;
    ID2D1SolidColorBrush* interim_brush_ = nullptr; // interim 半透明样式'''
new = '''    ID2D1SolidColorBrush* text_brush_ = nullptr;
    ID2D1SolidColorBrush* interim_brush_ = nullptr; // interim 半透明样式
    ID2D1SolidColorBrush* stroke_brush_ = nullptr;  // 描边色（默认黑）'''
assert old in s, 'h brush'
s = s.replace(old, new)
old = '''        bool show_status = true;
        int fade_in_ms  = 300;           // 字幕出现渐入时长
        int fade_out_ms = 500;           // 字幕消失渐出时长
    };'''
new = '''        bool show_status = true;
        int fade_in_ms  = 300;           // 字幕出现渐入时长
        int fade_out_ms = 500;           // 字幕消失渐出时长
        bool stroke_enabled = true;      // 文字描边（艺术字效果）
        DWORD stroke_color  = 0xFF000000; // 描边颜色 AARRGGBB（默认黑）
        int stroke_width    = 2;         // 描边粗细（像素）
    };'''
assert old in s, 'h style'
s = s.replace(old, new)
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('subtitle_window.h OK')

p = r'src\ui\subtitle_window.cpp'
s = open(p, encoding='utf-8').read()

# 1. release_d2d 释放 stroke_brush_
old = '''    if (interim_brush_)  { interim_brush_->Release();  interim_brush_  = nullptr; }'''
new = '''    if (interim_brush_)  { interim_brush_->Release();  interim_brush_  = nullptr; }
    if (stroke_brush_)   { stroke_brush_->Release();   stroke_brush_   = nullptr; }'''
assert old in s, 'release'
s = s.replace(old, new)

# 2. apply_style 创建 stroke_brush_
old = '''            // interim（未确认尾部）：文字色 45% 不透明
            target_->CreateSolidColorBrush(
                D2D1::ColorF((style_.font_color >> 16 & 0xFF) / 255.0f,
                             (style_.font_color >> 8 & 0xFF) / 255.0f,
                             (style_.font_color & 0xFF) / 255.0f,
                             ((style_.font_color >> 24 & 0xFF) / 255.0f) * 0.45f),
                &interim_brush_);'''
new = '''            // interim（未确认尾部）：文字色 45% 不透明
            target_->CreateSolidColorBrush(
                D2D1::ColorF((style_.font_color >> 16 & 0xFF) / 255.0f,
                             (style_.font_color >> 8 & 0xFF) / 255.0f,
                             (style_.font_color & 0xFF) / 255.0f,
                             ((style_.font_color >> 24 & 0xFF) / 255.0f) * 0.45f),
                &interim_brush_);
            // 描边色（艺术字效果，默认黑）
            target_->CreateSolidColorBrush(
                D2D1::ColorF((style_.stroke_color >> 16 & 0xFF) / 255.0f,
                             (style_.stroke_color >> 8 & 0xFF) / 255.0f,
                             (style_.stroke_color & 0xFF) / 255.0f,
                             (style_.stroke_color >> 24 & 0xFF) / 255.0f),
                &stroke_brush_);'''
assert old in s, 'stroke brush'
s = s.replace(old, new)

# 3. render：先 8 方向画描边，再原位画文本
old = '''        if (layout_) {
            target_->DrawTextLayout(D2D1::Point2F(0, 0), layout_, text_brush_,
                                    D2D1_DRAW_TEXT_OPTIONS_CLIP);
        } else if (!full.empty() && text_format_) {'''
new = '''        if (layout_) {
            // 描边（艺术字）：8 方向偏移绘制描边色，再原位绘制文字色
            if (style_.stroke_enabled && stroke_brush_ && style_.stroke_width > 0) {
                const float sw = (float)style_.stroke_width;
                const float offs[8][2] = {
                    {-sw, 0}, {sw, 0}, {0, -sw}, {0, sw},
                    {-sw, -sw}, {sw, -sw}, {-sw, sw}, {sw, sw},
                };
                for (auto& o : offs) {
                    target_->DrawTextLayout(D2D1::Point2F(o[0], o[1]), layout_,
                                            stroke_brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                }
            }
            target_->DrawTextLayout(D2D1::Point2F(0, 0), layout_, text_brush_,
                                    D2D1_DRAW_TEXT_OPTIONS_CLIP);
        } else if (!full.empty() && text_format_) {'''
assert old in s, 'render stroke'
s = s.replace(old, new)

open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('subtitle_window.cpp OK')

# 4. app.cpp：Style 传描边配置（两处管线窗口）
p = r'src\app.cpp'
s = open(p, encoding='utf-8').read()
old = '''    st.show_status   = cfg_.show_status;
    st.fade_in_ms    = cfg_.fade_in_ms;
    st.fade_out_ms   = cfg_.fade_out_ms;
    st.fps           = cfg_.fps;
    const int px = is_mic ? cfg_.mic_pos_x : cfg_.pc_pos_x;'''
new = '''    st.show_status   = cfg_.show_status;
    st.fade_in_ms    = cfg_.fade_in_ms;
    st.fade_out_ms   = cfg_.fade_out_ms;
    st.fps           = cfg_.fps;
    st.stroke_enabled = cfg_.stroke_enabled;
    st.stroke_color   = parse_color(cfg_.stroke_color).value_or(0xFF000000);
    st.stroke_width   = cfg_.stroke_width;
    const int px = is_mic ? cfg_.mic_pos_x : cfg_.pc_pos_x;'''
assert old in s, 'app style'
s = s.replace(old, new)
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('app.cpp OK')
