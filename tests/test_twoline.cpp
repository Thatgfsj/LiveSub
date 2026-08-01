// 上一句+当前句 两行结构测试
#include "asr/text_merge.h"
#include <cstdio>
static int fails = 0;
static void check(bool c, const char* n) { printf("%s %s\n", c ? "PASS" : "FAIL", n); if (!c) fails++; }
int main() {
    TextMerger m; m.set_max_lines(2);
    m.update("同学们，老师们。", true, 1000);
    check(m.current() == "同学们，老师们。", "定稿后单行");
    // 停顿后新句 → 上一句第一行 + 当前句第二行
    m.update("今天天气不错", false, 3000);
    check(m.current() == "同学们，老师们。\n今天天气不错", "上一句+当前句两行");
    // 当前句完善
    m.update("今天天气不错，适合直播。", false, 4000);
    check(m.current() == "同学们，老师们。\n今天天气不错，适合直播。", "当前句更新");
    // 定稿新句 → 历史推进；prune 后只保留最近两句
    m.update("今天天气不错，适合直播。", true, 5000);
    m.update("我们开始吧", false, 6000);
    m.prune(7000); // 最旧句超时滚出
    check(m.current() == "今天天气不错，适合直播。\n我们开始吧", "历史滚动到最近两句");
    // 旧句尾巴丢弃仍生效（针对最后定稿句）
    m.update("适合直播。", false, 8000);
    check(m.current() == "今天天气不错，适合直播。\n我们开始吧", "旧句尾巴不闪回");
    printf(fails ? "\n%d FAIL\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
