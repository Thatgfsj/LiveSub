// 上一句+当前句 两行结构测试（含扩展合并）
#include "asr/text_merge.h"
#include <cstdio>
static int fails = 0;
static void check(bool c, const char* n) { printf("%s %s\n", c ? "PASS" : "FAIL", n); if (!c) fails++; }
int main() {
    TextMerger m; m.set_max_lines(2);
    m.update("同学们，老师们。", true, 1000);
    check(m.current() == "同学们，老师们。", "定稿后单行");
    m.update("今天天气不错", false, 3000);
    check(m.current() == "同学们，老师们。\n今天天气不错", "上一句+当前句两行");
    m.update("今天天气不错，适合直播。", false, 4000);
    check(m.current() == "同学们，老师们。\n今天天气不错，适合直播。", "当前句更新");
    m.update("今天天气不错，适合直播。", true, 5000);
    m.update("我们开始吧", false, 6000);
    m.prune(7000);
    check(m.current() == "今天天气不错，适合直播。\n我们开始吧", "历史滚动到最近两句");
    m.update("适合直播。", false, 8000);
    check(m.current() == "今天天气不错，适合直播。\n我们开始吧", "旧句尾巴不闪回");

    // 当前句是上一句的扩展 → 合并为一行（解决两行开头相同）
    m.update("今天天气不错，适合直播。", true, 9000);
    m.update("今天天气不错，适合直播，欢迎大家收看。", false, 10000);
    check(m.current() == "今天天气不错，适合直播，欢迎大家收看。", "扩展句合并单行");

    // 完全不同内容 → 历史+当前句分行（渲染层负责裁剪到 2 行）
    m.update("今天天气不错，适合直播，欢迎大家收看。", true, 11000);
    m.update("接下来我们聊聊字幕技术", false, 12000);
    check(m.current() == "今天天气不错，适合直播。\n今天天气不错，适合直播，欢迎大家收看。\n接下来我们聊聊字幕技术",
          "不同内容分行");
    printf(fails ? "\n%d FAIL\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
