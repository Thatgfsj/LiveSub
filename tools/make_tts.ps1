# 生成介绍视频旁白（Windows SAPI 中文 TTS）→ build-video/tts/pN.wav
Add-Type -AssemblyName System.Speech
$ErrorActionPreference = "Stop"

$dir = "O:\clawwork\chuansongmen\LiveSub\build-video\tts"
New-Item -ItemType Directory -Force -Path $dir | Out-Null

$lines = @(
    "LiveSub，直播实时字幕工具。本地 AI 语音识别，纯本地运行。",
    "麦克风或电脑声音，实时转成字幕，延迟不到两秒。纯本地推理，不上传云端。",
    "音频采集，语音检测，分窗，本地模型识别，最后显示在字幕窗口。",
    "字幕窗口，透明置顶，字体描边位置都可以调。OBS 窗口捕获直接叠加进直播画面。",
    "右下角托盘，状态一目了然。语音输入，讲话稿记录，字幕快捷开关。",
    "模型下载器，大模型更准，小模型更快。双镜像，断点续传，断线自动重试。",
    "纯本地，低延迟，免费开源。GitHub 搜索 LiveSub 获取安装包。"
)

$syn = New-Object System.Speech.Synthesis.SpeechSynthesizer
$syn.Rate = 0
$syn.Volume = 100

# 优先用中文语音，找不到就用默认
$zhVoice = $syn.GetInstalledVoices() | Where-Object { $_.VoiceInfo.Culture.Name -like "zh*" } | Select-Object -First 1
if ($zhVoice) {
    $syn.SelectVoice($zhVoice.VoiceInfo.Name)
    Write-Host "使用语音: $($zhVoice.VoiceInfo.Name)"
} else {
    Write-Host "未找到中文语音，使用默认语音"
}

for ($i = 0; $i -lt $lines.Count; $i++) {
    $out = Join-Path $dir ("p{0}.wav" -f ($i + 1))
    $syn.SetOutputToWaveFile($out)
    $syn.Speak($lines[$i])
    $syn.SetOutputToNull()
    Write-Host ("p{0}: {1}" -f ($i + 1), $lines[$i])
}
$syn.Dispose()
Write-Host "TTS 生成完成"
