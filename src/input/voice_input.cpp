#include "voice_input.h"

#include <vector>

#include <windows.h>

#include "config.h"

void VoiceInput::commit_text(const std::string& utf8) {
    if (!enabled_.load() || utf8.empty()) return;

    // UTF-8 → UTF-16
    const std::wstring w = utf8_to_wide(utf8);
    if (w.empty()) return;

    // 逐字符注入（KEYEVENTF_UNICODE 直接输入字符，不经过键盘布局）
    std::vector<INPUT> inputs;
    inputs.reserve(w.size() * 2);
    for (wchar_t ch : w) {
        INPUT down = {};
        down.type = INPUT_KEYBOARD;
        down.ki.wVk = 0;
        down.ki.wScan = (WORD)ch;
        down.ki.dwFlags = KEYEVENTF_UNICODE;
        inputs.push_back(down);

        INPUT up = {};
        up.type = INPUT_KEYBOARD;
        up.ki.wVk = 0;
        up.ki.wScan = (WORD)ch;
        up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        inputs.push_back(up);
    }
    if (!inputs.empty()) {
        SendInput((UINT)inputs.size(), inputs.data(), sizeof(INPUT));
    }
}
