// 复现模型检测逻辑（debug，64 位文件大小）
#include <cstdio>
#include <string>
#include <windows.h>
#include "config.h"

int main() {
    const std::string mp = resolve_path("model");
    const std::string main_f = mp + "\\Qwen3-ASR-1.7B-Q8_0.gguf";
    const std::string sml_f  = mp + "\\Qwen3-ASR-0.6B-Q8_0.gguf";
    printf("mp=%s\n", mp.c_str());
    printf("main_f=%s\n", main_f.c_str());

    bool has = false;
    FILE* f = fopen(main_f.c_str(), "rb");
    if (f) {
        _fseeki64(f, 0, SEEK_END);
        long long sz = _ftelli64(f);
        printf("main 大小(64位)=%lld\n", sz);
        has = sz > 1000000000;
        fclose(f);
    } else {
        printf("main 打开失败\n");
    }
    printf("has_model=%d\n", (int)has);
    return 0;
}
