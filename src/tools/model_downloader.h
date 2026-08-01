#pragma once
// 模型下载器（独立小工具 + 主程序可复用）
// 功能：断点续传、多镜像重试、进度回调、文件大小校验
#include <cstdint>
#include <functional>
#include <string>

struct DownloadFile {
    std::string url;        // 主源（先尝试）
    std::string mirror;     // 备源
    std::string path;       // 保存路径
    uint64_t expected_size; // 期望大小（校验用，0=跳过）
};

// 下载（支持断点续传）。返回 0 成功；进度回调(下载字节, 总字节)
// on_progress 返回 false 可取消
int download_file(const DownloadFile& f,
                  const std::function<bool(uint64_t done, uint64_t total)>& on_progress,
                  std::string* err = nullptr);
