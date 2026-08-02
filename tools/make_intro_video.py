# LiveSub 项目介绍视频 · 幻灯片生成（1280x720，7 页）
# 无任何隐私信息：不含用户名/邮箱/路径/凭据
from PIL import Image, ImageDraw, ImageFont

W, H = 1280, 720
FONT_TITLE = "C:/Windows/Fonts/msyh.ttc"
FONT_BODY = "C:/Windows/Fonts/msyh.ttc"
FONT_MONO = "C:/Windows/Fonts/consola.ttf"

def grad_bg():
    """深蓝紫渐变背景"""
    img = Image.new("RGB", (W, H))
    px = img.load()
    for y in range(H):
        t = y / H
        r = int(34 + 34 * t)
        g = int(44 + 28 * t)
        b = int(86 + 66 * t)
        for x in range(W):
            px[x, y] = (r, g, b)
    return img

def font(size, bold=False):
    return ImageFont.truetype(FONT_TITLE, size)

def center_text(d, cx, y, text, f, color=(255, 255, 255)):
    bb = d.textbbox((0, 0), text, font=f)
    w = bb[2] - bb[0]
    d.text((cx - w / 2 - bb[0], y - bb[1]), text, font=f, fill=color)

def page_title(d, text):
    center_text(d, W // 2, 70, text, font(52), (255, 255, 255))
    # 标题下分隔线
    d.rectangle([W // 2 - 180, 150, W // 2 + 180, 156], fill=(120, 140, 255))

def bullet(d, y, text, f, color=(222, 226, 240), mark="•", mx=180):
    bb = d.textbbox((0, 0), mark, font=f)
    d.text((mx - 30 - bb[0], y - bb[1]), mark, font=f, fill=(120, 170, 255))
    d.text((mx + 10 - bb[0], y - bb[1]), text, font=f, fill=color)

def arrow(d, x1, y, x2, color=(120, 170, 255), w=6):
    d.line([x1, y, x2 - 18, y], fill=color, width=w)
    d.polygon([(x2 - 18, y - 12), (x2, y), (x2 - 18, y + 12)], fill=color)

def chip(d, cx, y, text, f, w, h=64):
    d.rounded_rectangle([cx - w // 2, y - h // 2, cx + w // 2, y + h // 2],
                        radius=16, fill=(40, 52, 96), outline=(120, 170, 255), width=2)
    center_text(d, cx, y - 14, text, font(24), (255, 255, 255))
    return y + h // 2

def chip_flow(d, items, y0):
    """一排流程芯片（带箭头）"""
    n = len(items)
    step = 230
    total = (n - 1) * step
    x0 = W // 2 - total // 2
    for i, t in enumerate(items):
        cx = x0 + i * step
        chip(d, cx, y0, t, font(26), 200, 60)
        if i < n - 1:
            arrow(d, cx + 105, y0, cx + step - 105, (120, 170, 255), 5)
    return y0 + 70

def make(page, out):
    img = grad_bg()
    d = ImageDraw.Draw(img)

    if page == 1:
        # ============ 封面 ============
        center_text(d, W // 2, 250, "LiveSub", font(110), (255, 255, 255))
        center_text(d, W // 2, 360, "直播实时字幕工具", font(48), (160, 200, 255))
        d.rounded_rectangle([W // 2 - 260, 430, W // 2 + 260, 500],
                            radius=30, fill=(58, 76, 140), outline=(120, 170, 255), width=3)
        center_text(d, W // 2, 462, "本地 AI 语音识别 · 纯本地运行", font(28), (255, 255, 255))
        center_text(d, W // 2, 570, "麦克风 / 电脑声音 → 实时字幕，OBS 窗口捕获直接叠加", font(26), (170, 178, 210))

    elif page == 2:
        # ============ 核心能力 ============
        page_title(d, "核心能力")
        bullet(d, 250, "实时字幕：说话即出字，端到端延迟 < 2 秒", font(36))
        bullet(d, 340, "双音源：麦克风字幕（讲解）+ 电脑声音字幕（视频/直播）", font(36))
        bullet(d, 430, "纯本地推理：本地模型识别，无云端上传，隐私安全", font(36))
        bullet(d, 520, "单引擎共享：双音源共用一份模型，显存占用低", font(36))

    elif page == 3:
        # ============ 技术架构 ============
        page_title(d, "技术架构")
        chip_flow(d, ["音频采集", "语音检测", "分窗", "本地 ASR", "字幕窗口"], 290)
        center_text(d, W // 2, 430, "音频采集：WASAPI（麦克风 / 电脑声音 loopback）", font(28), (190, 198, 228))
        center_text(d, W // 2, 480, "语音检测：VAD 自适应门限，只识别有效语音段", font(28), (190, 198, 228))
        center_text(d, W // 2, 530, "本地 ASR：Qwen3-ASR 模型（Vulkan GPU 加速，CPU 可回退）", font(28), (190, 198, 228))
        center_text(d, W // 2, 580, "字幕窗口：Direct2D 透明渲染，稳定不回退的流式字幕", font(28), (190, 198, 228))

    elif page == 4:
        # ============ 字幕窗口 ============
        page_title(d, "字幕窗口（展示框）")
        bullet(d, 240, "透明置顶：悬浮于任何画面之上，供 OBS 窗口捕获", font(34))
        bullet(d, 320, "字体样式：字号 / 颜色 / 背景透明度 / 艺术字描边", font(34))
        bullet(d, 400, "位置像素级自定义（中心坐标），点击穿透不挡操作", font(34))
        bullet(d, 480, "最多两行：上一句 + 当前句，长句自动缩小字号", font(34))

    elif page == 5:
        # ============ 托盘交互 ============
        page_title(d, "托盘集成（右下角）")
        bullet(d, 240, "状态灯：蓝=就绪  绿=识别中  红=错误", font(34))
        bullet(d, 320, "语音输入：定稿句直接输入当前焦点窗口（听写）", font(34))
        bullet(d, 400, "讲话稿记录：按时间命名存到桌面，随时回顾", font(34))
        bullet(d, 480, "快捷切换：麦克风 / 电脑字幕开关即点即用", font(34))

    elif page == 6:
        # ============ 模型下载器 ============
        page_title(d, "模型下载器")
        chip_flow(d, ["大模型 1.7B", "更准确 · 推荐", "约 2.8GB"], 250)
        chip_flow(d, ["小模型 0.6B", "更快 · 低要求", "约 1.1GB"], 400)
        bullet(d, 540, "双镜像源 + 断点续传，断线自动重试，损坏自动重下", font(30))

    elif page == 7:
        # ============ 结尾 ============
        center_text(d, W // 2, 260, "纯本地 · 低延迟 · 免费开源", font(56), (255, 255, 255))
        center_text(d, W // 2, 370, "直播 / 录播 / 视频会议 皆可用", font(36), (160, 200, 255))
        center_text(d, W // 2, 500, "GitHub 搜索 LiveSub 获取安装包", font(32), (170, 178, 210))

    img.save(out)
    print("saved", out)

import os
os.makedirs("build-video/frames", exist_ok=True)
for p in range(1, 8):
    make(p, f"build-video/frames/p{p}.png")
print("done")
