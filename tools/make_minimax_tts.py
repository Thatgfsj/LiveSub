# -*- coding: utf-8 -*-
"""MiniMax TTS 生成介绍视频旁白 → build-video/tts2/pN.mp3"""
import os, json, time, urllib.request, urllib.error

ENDPOINT = "https://api.minimaxi.com/v1/t2a_v2"
API_KEY = os.environ.get("MINIMAX_API_KEY", "")
VOICE = "female-chengshu-jingpin"  # 成熟女声

LINES = [
    "直播的时候，观众听不清你说话怎么办？LiveSub，让字幕自己出现。",
    "以前做字幕，要人工打字，要付费服务，还要等剪辑。现在，你只需要说话。",
    "打开 LiveSub，对着麦克风开口，大约一秒，字幕就出现在屏幕上。全程本地识别，你的声音不上传任何地方。",
    "直播讲解，开麦克风字幕。看视频看直播，开电脑声音字幕。两个场景，一个窗口，位置样式随你调。",
    "OBS 里添加窗口捕获，字幕直接叠进直播画面，观众看得清清楚楚。鼠标穿透不挡操作，置顶显示不遮挡。",
    "它还不止字幕。说话代替打字，直接输入聊天框。直播结束，讲话稿已经整理好在桌面。",
    "纯本地，低延迟，免费开源。GitHub 搜索 LiveSub，下一个直播，就让它替你打字幕。",
]

OUT = r"O:\clawwork\chuansongmen\LiveSub\build-video\tts2"
os.makedirs(OUT, exist_ok=True)


def synth(text, out_path):
    body = {
        "model": "speech-2.8-hd",
        "text": text,
        "stream": False,
        "voice_setting": {"voice_id": VOICE, "speed": 1.0, "vol": 1.0, "pitch": 0},
    }
    req = urllib.request.Request(
        ENDPOINT,
        data=json.dumps(body).encode("utf-8"),
        headers={
            "Content-Type": "application/json",
            "Authorization": "Bearer " + API_KEY,
        },
    )
    with urllib.request.urlopen(req, timeout=120) as resp:
        data = json.loads(resp.read().decode("utf-8"))
    if data.get("base_resp", {}).get("status_code", 0) != 0:
        raise RuntimeError("API 错误: " + json.dumps(data, ensure_ascii=False))
    audio_hex = data.get("data", {}).get("audio")
    if not audio_hex:
        raise RuntimeError("响应无 audio: " + json.dumps(data, ensure_ascii=False)[:300])
    # MiniMax 返回 hex 编码的 MP3（以 ID3 头 494433 开头）
    with open(out_path, "wb") as f:
        f.write(bytes.fromhex(audio_hex))
    print("ok:", os.path.basename(out_path))


for i, line in enumerate(LINES):
    p = os.path.join(OUT, "p%d.mp3" % (i + 1))
    for attempt in range(3):
        try:
            synth(line, p)
            break
        except Exception as e:
            print("retry", i + 1, attempt, e)
            time.sleep(3)
    time.sleep(0.5)
print("TTS 完成")
