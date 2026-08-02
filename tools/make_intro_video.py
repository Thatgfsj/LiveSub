# LiveSub 介绍视频 · 幻灯片生成 v2（现代设计 + 模拟字幕条）
# 1280x720，7 页；每页底部一条 LiveSub 风格字幕条（旁白核心句）
from PIL import Image, ImageDraw, ImageFont

W, H = 1280, 720
FONT = "C:/Windows/Fonts/msyh.ttc"

# ---- 调色板（现代暗色 + 青蓝主色） ----
BG_TOP = (18, 22, 36)      # 近黑蓝
BG_BOT = (10, 12, 22)
ACCENT = (80, 140, 255)    # 主青蓝
ACCENT2 = (140, 100, 255)  # 紫
TEXT_MAIN = (240, 244, 252)
TEXT_SUB = (168, 178, 200)
CARD_BG = (28, 34, 54)
CARD_LINE = (70, 90, 150)

def font(size, bold=False):
    return ImageFont.truetype(FONT, size)

def grad_bg():
    img = Image.new("RGB", (W, H))
    px = img.load()
    for y in range(H):
        t = y / H
        r = int(BG_TOP[0] + (BG_BOT[0] - BG_TOP[0]) * t)
        g = int(BG_TOP[1] + (BG_BOT[1] - BG_TOP[1]) * t)
        b = int(BG_TOP[2] + (BG_BOT[2] - BG_TOP[2]) * t)
        for x in range(W):
            px[x, y] = (r, g, b)
    return img

def center_text(d, cx, y, text, f, color=TEXT_MAIN):
    bb = d.textbbox((0, 0), text, font=f)
    d.text((cx - (bb[2] - bb[0]) / 2 - bb[0], y - bb[1]), text, font=f, fill=color)

def page_title(d, text, sub=None):
    center_text(d, W // 2, 56, text, font(46), TEXT_MAIN)
    if sub:
        center_text(d, W // 2, 118, sub, font(22), TEXT_SUB)
    # 标题下渐变短横线
    d.rounded_rectangle([W // 2 - 60, 152, W // 2 + 60, 156], radius=2, fill=ACCENT)

def card(d, cx, cy, w, h, text_lines, title=None, accent=ACCENT, radius=20):
    """现代卡片：深底 + 细描边 + 主色顶条；text_lines 居中"""
    x0, y0 = cx - w // 2, cy - h // 2
    d.rounded_rectangle([x0, y0, x0 + w, y0 + h], radius=radius,
                        fill=CARD_BG, outline=CARD_LINE, width=2)
    d.rounded_rectangle([x0, y0, x0 + w, y0 + 6], radius=radius, fill=accent)
    if title:
        center_text(d, cx, y0 + 30, title, font(24), accent)
    n = len(text_lines)
    for i, ln in enumerate(text_lines):
        center_text(d, cx, y0 + 60 + i * 44, ln, font(26), TEXT_MAIN if i == 0 else TEXT_SUB)

def subtitle_bar(d, text, y=600):
    """模拟 LiveSub 字幕条：深色圆角条 + 白字 + 描边效果（产品演示）"""
    f = font(30)
    bb = d.textbbox((0, 0), text, font=f)
    tw = bb[2] - bb[0]
    bar_w = tw + 90
    x0, y0 = W // 2 - bar_w // 2, y
    d.rounded_rectangle([x0, y0, x0 + bar_w, y0 + 64], radius=14,
                        fill=(0, 0, 0), outline=(60, 70, 100), width=2)
    # 白字 + 黑描边（艺术字效果）
    for dx, dy in [(-2, 0), (2, 0), (0, -2), (0, 2), (-2, -2), (2, 2), (-2, 2), (2, -2)]:
        d.text((W // 2 - tw / 2 + dx - bb[0], y0 + 14 - bb[1]), text, font=f, fill=(0, 0, 0))
    d.text((W // 2 - tw / 2 - bb[0], y0 + 14 - bb[1]), text, font=f, fill=(255, 255, 255))

def chip_flow(d, items, y0, accent=ACCENT):
    """一排流程卡片（带箭头）"""
    n = len(items)
    step = 250
    x0 = W // 2 - (n - 1) * step // 2
    for i, t in enumerate(items):
        cx = x0 + i * step
        d.rounded_rectangle([cx - 105, y0 - 32, cx + 105, y0 + 32], radius=16,
                            fill=CARD_BG, outline=CARD_LINE, width=2)
        center_text(d, cx, y0 - 9, t, font(24), TEXT_MAIN)
        if i < n - 1:
            ax = cx + 105
            d.line([ax, y0, ax + 40, y0], fill=ACCENT, width=5)
            d.polygon([(ax + 40, y0 - 9), (ax + 52, y0), (ax + 40, y0 + 9)], fill=ACCENT)
    return y0 + 70

def make(page, out):
    img = grad_bg()
    d = ImageDraw.Draw(img)

    if page == 1:
        # ============ 封面 ============
        center_text(d, W // 2, 200, "LiveSub", font(120), TEXT_MAIN)
        center_text(d, W // 2, 320, "直播实时字幕", font(52), ACCENT)
        # 装饰：顶部光斑
        d.ellipse([W // 2 - 260, 120, W // 2 + 260, 340], outline=(40, 60, 120), width=2)
        subtitle_bar(d, "直播说话，字幕自动出现")

    elif page == 2:
        # ============ 痛点 ============
        page_title(d, "观众听不清你说话？", "直播环境嘈杂 · 口音 · 语速 · 回声")
        card(d, W // 2, 330, 720, 200, ["以前：人工打字、付费服务、等剪辑"], title="过去", accent=(120, 130, 150))
        card(d, W // 2, 560, 720, 130, ["现在：打开就说话，字幕自己出现"], title="现在", accent=ACCENT)
        subtitle_bar(d, "不用打字，不用剪辑")

    elif page == 3:
        # ============ 怎么用 ============
        page_title(d, "怎么用？打开，说话", "三步开始")
        chip_flow(d, ["打开 LiveSub", "对着麦克风说话", "字幕上屏"], 300)
        card(d, W // 2, 480, 760, 150, ["约 1 秒出字，边说边出", "全程本地识别，声音不上传"], accent=ACCENT)
        subtitle_bar(d, "你的声音，不出你的电脑")

    elif page == 4:
        # ============ 两个场景 ============
        page_title(d, "两个场景，一个窗口", "直播讲解 / 看视频看直播")
        card(d, 320, 320, 420, 220, ["直播讲解", "麦克风字幕"], title="场景一", accent=ACCENT)
        card(d, 960, 320, 420, 220, ["看视频 / 直播", "电脑声音字幕"], title="场景二", accent=ACCENT2)
        center_text(d, W // 2, 500, "字幕窗口透明置顶，位置样式随你调", font(26), TEXT_SUB)
        subtitle_bar(d, "两个场景，一个窗口")

    elif page == 5:
        # ============ OBS ============
        page_title(d, "直接进直播画面")
        chip_flow(d, ["OBS 窗口捕获", "选择 LiveSub 字幕", "叠加进直播"], 300)
        card(d, W // 2, 490, 760, 140, ["字体、描边、位置、透明度，按你的风格来", "鼠标穿透不挡操作"], accent=ACCENT)
        subtitle_bar(d, "观众看得清清楚楚")

    elif page == 6:
        # ============ 额外价值 ============
        page_title(d, "还不止字幕")
        card(d, 320, 320, 420, 220, ["语音输入", "说话代替打字，直接输入聊天框"], title="听写", accent=ACCENT)
        card(d, 960, 320, 420, 220, ["讲话稿记录", "直播结束，文稿已经在桌面"], title="记录", accent=ACCENT2)
        center_text(d, W // 2, 520, "中英日韩自动识别", font(26), TEXT_SUB)
        subtitle_bar(d, "说完，就都有了")

    elif page == 7:
        # ============ 结尾 ============
        center_text(d, W // 2, 240, "纯本地 · 低延迟 · 免费开源", font(52), TEXT_MAIN)
        center_text(d, W // 2, 330, "下一个直播，就让它替你打字幕", font(32), ACCENT)
        d.rounded_rectangle([W // 2 - 220, 400, W // 2 + 220, 470], radius=24,
                            fill=CARD_BG, outline=ACCENT, width=2)
        center_text(d, W // 2, 432, "GitHub 搜索 LiveSub", font(30), TEXT_MAIN)
        subtitle_bar(d, "下载安装包，两分钟用上")

    img.save(out)
    print("saved", out)

import os
os.makedirs("build-video/frames", exist_ok=True)
for p in range(1, 8):
    make(p, f"build-video/frames/p{p}.png")
print("done")
