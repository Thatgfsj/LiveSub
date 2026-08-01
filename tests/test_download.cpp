// 下载逻辑控制台测试
#include <cstdio>
#include "tools/model_downloader.h"

int main() {
    DownloadFile f;
    f.url = "https://hf-mirror.com/ggml-org/Qwen3-ASR-1.7B-GGUF/resolve/main/mmproj-Qwen3-ASR-1.7B-bf16.gguf";
    f.mirror = "";
    f.path = "C:\\Users\\thatg\\AppData\\Local\\Temp\\dl_test.gguf";
    f.expected_size = 641773984ull;
    std::string err;
    int rc = download_file(f, [](uint64_t d, uint64_t t) -> bool {
        static uint64_t last = 0;
        if (d - last > 8 * 1024 * 1024) {
            last = d;
            printf("  %llu / %llu MB\n", (unsigned long long)(d / 1048576), (unsigned long long)(t / 1048576));
        }
        return true;
    }, &err);
    printf("rc=%d err=%s\n", rc, err.c_str());
    return 0;
}
