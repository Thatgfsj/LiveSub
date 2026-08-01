# -*- coding: utf-8 -*-
# 生成 LiveSub 图标（livesub.ico，含 16/32/48/256）
# 设计：蓝紫渐变圆角方块 + 白色字幕三条线（直播字幕风格）
# 纯 Python 实现（超采样抗锯齿 + PNG/BMP 编码），无第三方依赖
import struct, zlib, os, math

OUT = os.path.join(os.path.dirname(__file__), '..', 'icons')
os.makedirs(OUT, exist_ok=True)
ICO_PATH = os.path.join(OUT, 'livesub.ico')

# ---------- 像素绘制（超采样 SS=8） ----------
SS = 8

def smoothstep(a, b, x):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)

def pixel_color(px, py):
    """返回 (r,g,b,a)，坐标 0..1"""
    # 圆角方形：圆角半径 0.22
    def in_round_rect(x, y, r):
        # 中心 0.5,0.5，尺寸 0.94
        cx, cy = 0.5, 0.5
        hw, hh = 0.47, 0.47
        dx = abs(x - cx) - (hw - r)
        dy = abs(y - cy) - (hh - r)
        if dx <= 0 and dy <= 0:
            return 1.0
        d = math.sqrt(max(dx, 0) ** 2 + max(dy, 0) ** 2)
        return 1.0 - smoothstep(r - 0.5 / SS, r + 0.5 / SS, d)

    bg = in_round_rect(px, py, 0.22)
    if bg <= 0:
        return (0, 0, 0, 0)

    # 蓝紫渐变（对角）
    g = smoothstep(0.0, 1.0, px * 0.7 + py * 0.3)
    r = int(79 + (124 - 79) * g)
    gg = int(70 + (58 - 70) * g)
    b = int(229 + (237 - 229) * g)

    # 三条白色字幕线（圆角横条）
    def line(x, y, y0, h, w):
        return in_round_rect2(x, y, y0, h, w)

    def in_round_rect2(x, y, y0, h, w):
        cx = 0.5
        r = h * 0.45
        hw = w / 2
        dx = abs(x - cx) - (hw - r)
        dy = abs(y - y0) - (h / 2 - r)
        if dx <= 0 and dy <= 0:
            return 1.0
        d = math.sqrt(max(dx, 0) ** 2 + max(dy, 0) ** 2)
        return 1.0 - smoothstep(r - 0.5 / SS, r + 0.5 / SS, d)

    lines = [
        (0.38, 0.68),   # 第一条线 y, 宽
        (0.50, 0.76),
        (0.62, 0.52),
    ]
    for y0, w in lines:
        if in_round_rect2(px, py, y0, 0.085, w) > 0.5:
            return (255, 255, 255, 255)
    return (r, gg, b, 255)

def render(size):
    img = []
    for y in range(size):
        row = []
        for x in range(size):
            # 超采样
            acc = [0, 0, 0, 0]
            for sy in range(SS):
                for sx in range(SS):
                    px = (x + (sx + 0.5) / SS) / size
                    py = (y + (sy + 0.5) / SS) / size
                    c = pixel_color(px, py)
                    for i in range(4):
                        acc[i] += c[i]
            n = SS * SS
            row.append((acc[0] // n, acc[1] // n, acc[2] // n, acc[3] // n))
        img.append(row)
    return img

# ---------- PNG 编码（256 用） ----------
def png_encode(img, size):
    def chunk(tag, data):
        c = tag + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    raw = b''
    for row in img:
        raw += b'\x00'
        for (r, g, b, a) in row:
            raw += bytes((r, g, b, a))
    ihdr = struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) +
            chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))

# ---------- BMP/DIB 编码（16/32/48 用，32bpp BGRA 上下颠倒） ----------
def dib_encode(img, size):
    # 自顶向下：BI_RGB 32bpp 要求自下而上；用负高度自顶向下
    header = struct.pack('<IiiHHIIiiII', 40, size, size, 1, 32, 0, 0, 0, 0, 0, 0)
    px = b''
    for row in img:
        for (r, g, b, a) in row:
            px += bytes((b, g, r, a))
    return header + px

# ---------- ICO 打包 ----------
sizes = [16, 32, 48, 256]
images = []
for s in sizes:
    img = render(s)
    if s == 256:
        data = png_encode(img, s)
        images.append((s, 0, data))
    else:
        data = dib_encode(img, s)
        images.append((s, 32, data))

# ICONDIR
header = struct.pack('<HHH', 0, 1, len(images))
entries = b''
offset = 6 + 16 * len(images)
for (s, bpp, data) in images:
    w = 0 if s == 256 else s
    entries += struct.pack('<BBBBHHII', w, w, 0, 0, 1, bpp, len(data), offset)
    offset += len(data)
with open(ICO_PATH, 'wb') as f:
    f.write(header + entries)
    for (_, _, data) in images:
        f.write(data)
print('生成:', ICO_PATH, os.path.getsize(ICO_PATH), 'bytes')
