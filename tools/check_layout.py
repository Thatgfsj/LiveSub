# 设置窗口布局精确模拟（按 settings_window.cpp 真实坐标公式）
# 检测：同页内控件重叠、超窗、hint 覆盖输入框
import unicodedata

def S(v, dpi):
    return v * dpi // 96

# hint 文本宽度估算（仿宋 14px @96dpi：全角≈14px，半角≈7px）
def hint_w(text, dpi):
    w = 0
    for ch in text:
        w += 14 if unicodedata.east_asian_width(ch) in 'WF' else 7
    return w * dpi // 96 + S(4, dpi)

def make(dpi):
    W = S(640, dpi); 
    LABEL_W, EDIT_W, PAD = S(150, dpi), S(80, dpi), S(12, dpi)
    PAGE_TOP = 36  # TCM_ADJUSTRECT(~24) + S(12) 间距
    c1, c1e = PAD + S(8, dpi), PAD + S(8, dpi) + LABEL_W + S(6, dpi)
    c2, c2e = S(340, dpi), S(340, dpi) + LABEL_W + S(6, dpi)
    def row(i): return PAGE_TOP + i * S(34, dpi)

    pages = [[], [], [], []]
    def add(page, name, x, y, w, h):
        pages[page].append((name, x, y, w, h))

    # ---- 页1 字幕显示（两列） ----
    p = 0
    for i, nm in enumerate(["字号", "文字颜色", "背景颜色", "透明度%(0-100)", "行数(1-6)"]):
        add(p, "label" + nm, c1, row(i) + S(4, dpi), LABEL_W, S(20, dpi))
        add(p, "edit" + nm, c1e, row(i), EDIT_W, S(22, dpi))
    add(p, "cb文字描边", c1, row(5), S(96, dpi), S(22, dpi))
    add(p, "edit描边色", c1e, row(5), EDIT_W, S(22, dpi))
    add(p, "label描边粗(1-8)", c1, row(6) + S(4, dpi), LABEL_W, S(20, dpi))
    add(p, "edit描边粗", c1e, row(6), EDIT_W, S(22, dpi))
    add(p, "label位置X", c2, row(0) + S(4, dpi), LABEL_W, S(20, dpi))
    add(p, "edit位置X", c2e, row(0), EDIT_W, S(22, dpi))
    add(p, "label位置Y", c2, row(1) + S(4, dpi), LABEL_W, S(20, dpi))
    add(p, "edit位置Y", c2e, row(1), EDIT_W, S(22, dpi))
    add(p, "cb置顶", c2, row(2), S(100, dpi), S(22, dpi))
    add(p, "cb点击穿透", c2 + S(110, dpi), row(2), S(110, dpi), S(22, dpi))
    add(p, "hint位置", c2 + S(8, dpi), row(3), hint_w("X=960 居中，Y=900 靠底", dpi), S(18, dpi))
    add(p, "hint穿透", c2 + S(8, dpi), row(4), hint_w("穿透后鼠标可正常操作直播软件", dpi), S(18, dpi))

    # ---- 页2 识别 ----
    p = 1
    for i, nm in enumerate(["VAD 阈值(dB)", "句末静音(ms)", "识别窗口(ms)", "窗口步长(ms)"]):
        add(p, "label" + nm, c1, row(i) + S(4, dpi), LABEL_W, S(20, dpi))
        add(p, "edit" + nm, c1e, row(i), EDIT_W, S(22, dpi))
    add(p, "hintVAD", c1e + EDIT_W + S(8, dpi), row(0), hint_w("说话触发门限（默认-52），越接近0越难触发", dpi), S(18, dpi))
    add(p, "label模型", c1, row(4) + S(4, dpi), LABEL_W, S(20, dpi))
    add(p, "radio大", c1e, row(4), S(150, dpi), S(22, dpi))
    add(p, "radio小", c1e + S(160, dpi), row(4), S(150, dpi), S(22, dpi))
    add(p, "hint模型", c1e + EDIT_W + S(8, dpi), row(5), hint_w("大≈2.8GB / 小≈1.1GB，保存后重启生效", dpi), S(18, dpi))
    add(p, "modelinfo", c1, row(6), W - PAD * 2 - c1, S(44, dpi))

    # ---- 页3 音频 ----
    p = 2
    add(p, "label麦克风", c1, row(0) + S(4, dpi), LABEL_W, S(20, dpi))
    add(p, "combo设备", c1e, row(0), EDIT_W + S(200, dpi), S(22, dpi))
    add(p, "btn刷新", c1e + EDIT_W + S(210, dpi), row(0), S(56, dpi), S(22, dpi))
    add(p, "cb麦克风字幕", c1, row(2), S(120, dpi), S(22, dpi))
    add(p, "cb电脑字幕", c1 + S(130, dpi), row(2), S(110, dpi), S(22, dpi))
    add(p, "hint电脑", c1 + S(250, dpi), row(2), hint_w("电脑字幕=识别电脑播放的声音（视频/直播）", dpi), S(18, dpi))
    add(p, "hint两条", c1, row(3), hint_w("两条字幕共用同一展示框，一般不同时开启；也可在托盘右键快速切换", dpi), S(18, dpi))

    # ---- 页4 输出 ----
    p = 3
    add(p, "cb写文本", c1, row(0), S(120, dpi), S(22, dpi))
    add(p, "hint写文本", c1 + S(130, dpi), row(0), hint_w("每次定稿句追加到 subtitles.txt", dpi), S(18, dpi))
    add(p, "cb写SRT", c1, row(1), S(120, dpi), S(22, dpi))
    add(p, "hint写SRT", c1 + S(130, dpi), row(1), hint_w("带时间轴的 subtitles.srt", dpi), S(18, dpi))
    add(p, "hint讲话稿", c1, row(3), hint_w("讲话稿记录（按时间命名存到桌面）在托盘菜单开启", dpi), S(18, dpi))
    return W, pages

def overlaps(a, b):
    ax, ay, aw, ah = a[1:]; bx, by, bw, bh = b[1:]
    ox = max(0, min(ax + aw, bx + bw) - max(ax, bx))
    oy = max(0, min(ay + ah, by + bh) - max(ay, by))
    return ox, oy

ok_all = True
for dpi in (96, 125, 150, 175, 200):
    W, pages = make(dpi)
    print(f"===== DPI {dpi}%  (窗口宽 {W}) =====")
    issues = 0
    for pi, ctl in enumerate(pages):
        for name, x, y, w, h in ctl:
            if x < 0 or y < 0 or x + w > W:
                print(f"  页{pi+1} [超窗] {name}: x={x} 右={x+w} 窗口宽={W}")
                issues += 1
        for i in range(len(ctl)):
            for j in range(i + 1, len(ctl)):
                ox, oy = overlaps(ctl[i], ctl[j])
                if ox > 2 and oy > 2:
                    print(f"  页{pi+1} [重叠] {ctl[i][0]} <-> {ctl[j][0]}: {ox}x{oy}")
                    issues += 1
    if not issues:
        print("  同页内无重叠、无超窗 ✓")
    else:
        ok_all = False
print("\n=== 结论:", "全部通过" if ok_all else "存在问题，见上 ===")
