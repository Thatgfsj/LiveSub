// 追加式更新测试（打字机效果：只增长不缩回）
#include "asr/text_merge.h"
#include <cstdio>
static int fails = 0;
static void check(bool c, const char* n) { printf("%s %s\n", c ? "PASS" : "FAIL", n); if (!c) fails++; }
int main() {
    TextMerger m; m.set_max_lines(2);
    m.update("大家好", false, 1000);
    check(m.current() == "大家好", "首字");
    m.update("大家好，今天", false, 2000);
    check(m.current() == "大家好，今天", "追加增长");
    m.update("大家好，今天我们来聊聊", false, 3000);
    check(m.current() == "大家好，今天我们来聊聊", "继续追加");
    // 识别回退（变短）→ 保持不缩回
    m.update("大家好", false, 4000);
    check(m.current() == "大家好，今天我们来聊聊", "回退不缩回");
    // 完全不同 → 替换（新句）
    m.update("今天天气不错", false, 5000);
    check(m.current() == "今天天气不错", "新句替换");
    // 标点差异的追加（"今天天气不错" → "今天天气不错。"）
    m.update("今天天气不错。", false, 6000);
    check(m.current() == "今天天气不错。", "标点后追加（去标点前缀一致）");
    printf(fails ? "\n%d FAIL\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
