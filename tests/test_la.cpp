// Local Agreement 行为验证
#include "asr/text_merge.h"
#include <cstdio>
int main() {
    TextMerger m; m.set_max_lines(2);
    m.update("大家好", false, 1000);
    printf("[1] '%s' conf=%zu\n", m.current().c_str(), m.confirmed_chars());
    m.update("大家好，今天", false, 2000);
    printf("[2] '%s' conf=%zu\n", m.current().c_str(), m.confirmed_chars());
    m.update("大家好，今天我们来聊聊", false, 3000);
    printf("[3] '%s' conf=%zu\n", m.current().c_str(), m.confirmed_chars());
    m.update("大家好", false, 4000);  // 回退
    printf("[4回退] '%s' conf=%zu\n", m.current().c_str(), m.confirmed_chars());
    m.update("今天天气不错", false, 5000);  // 新句
    printf("[5新句] '%s' conf=%zu\n", m.current().c_str(), m.confirmed_chars());
    return 0;
}
