#include "model_downloader.h"

#include <cstdio>
#include <vector>

#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

// 单次请求：把响应流式写入文件（边读边写，实时进度；支持 Range 续传）
// 返回 0=成功；1=需要重试（未完成/网络中断）；-1=失败（换镜像）
static int http_get_to_file(const std::string& url, uint64_t resume_from,
                            const std::string& path, uint64_t expected_size,
                            const std::function<bool(uint64_t, uint64_t)>& on_progress,
                            std::string* err, int redirects_left) {
    WCHAR host[256] = {}, pathw[1024] = {};
    URL_COMPONENTSW comp = {};
    comp.dwStructSize = sizeof(comp);
    comp.lpszHostName = host; comp.dwHostNameLength = 256;
    comp.lpszUrlPath = pathw;  comp.dwUrlPathLength = 1024;
    const std::wstring wurl(url.begin(), url.end());
    if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &comp)) {
        if (err) *err = "URL 解析失败";
        return -1;
    }

    HINTERNET ses = WinHttpOpen(L"LiveSub-ModelDownloader/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) { if (err) *err = "WinHTTP 初始化失败"; return -1; }
    WinHttpSetTimeouts(ses, 10000, 15000, 15000, 15000);
    HINTERNET con = WinHttpConnect(ses, host, comp.nPort, 0);
    if (!con) { WinHttpCloseHandle(ses); if (err) *err = "连接失败"; return -1; }

    HINTERNET req = WinHttpOpenRequest(con, L"GET", pathw, nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       comp.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!req) { WinHttpCloseHandle(con); WinHttpCloseHandle(ses); if (err) *err = "创建请求失败"; return -1; }

    if (resume_from > 0) {
        const std::wstring range = L"bytes=" + std::to_wstring(resume_from) + L"-";
        WinHttpAddRequestHeaders(req, range.c_str(), (DWORD)range.size(), WINHTTP_ADDREQ_FLAG_REPLACE | WINHTTP_ADDREQ_FLAG_ADD);
    }
    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
        if (err) *err = "发送请求失败";
        return -1;
    }
    if (!WinHttpReceiveResponse(req, nullptr)) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
        if (err) *err = "接收响应失败";
        return -1;
    }

    DWORD status = 0, status_len = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_len, WINHTTP_NO_HEADER_INDEX);

    // 重定向（WinHTTP 默认自动跟随；这里兜底处理 3xx）
    if (status >= 300 && status < 400) {
        WCHAR loc[2048] = {};
        DWORD loc_len = sizeof(loc);
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                loc, &loc_len, WINHTTP_NO_HEADER_INDEX)) {
            WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
            if (redirects_left <= 0) {
                if (err) *err = "重定向次数过多";
                return -1;
            }
            return http_get_to_file(std::string(loc, loc + wcslen(loc)), resume_from,
                                    path, expected_size, on_progress, err, redirects_left - 1);
        }
        WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
        if (err) *err = "HTTP " + std::to_string(status);
        return -1;
    }
    if (status != 200 && status != 206) {
        if (err) *err = "HTTP " + std::to_string(status);
        WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
        return -1;
    }

    // 总长度（断点续传时 Content-Length 是剩余段长）
    uint64_t total = 0;
    WCHAR clen[64] = {};
    DWORD clen_len = sizeof(clen);
    if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                            clen, &clen_len, WINHTTP_NO_HEADER_INDEX)) {
        total = _wtoi64(clen);
        if (status == 206 && resume_from > 0 && total > 0) total += resume_from;
        if (expected_size > 0 && total == 0) total = expected_size;
    } else if (expected_size > 0) {
        total = expected_size; // 服务端未知总长：用期望大小显示进度
    }

    // 边读边写（续传模式）
    FILE* fp = fopen(path.c_str(), resume_from > 0 ? "ab" : "wb");
    if (!fp) { WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
               if (err) *err = "无法创建文件: " + path; return -1; }
    if (resume_from == 0) {
        // 全新下载：先清空
        fclose(fp);
        fp = fopen(path.c_str(), "wb");
    }

    uint64_t done = resume_from;
    char buf[128 * 1024];
    DWORD got = 0;
    uint64_t last_report = 0;
    bool ok = true;
    while (WinHttpReadData(req, buf, sizeof(buf), &got) && got > 0) {
        fwrite(buf, 1, got, fp);
        done += got;
        got = 0;
        // 进度回调（节流：每 256KB）
        if (done - last_report >= 256 * 1024) {
            last_report = done;
            if (on_progress && !on_progress(done, total)) { ok = false; break; }
        }
    }
    fflush(fp);
    fclose(fp);
    WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);

    if (!ok) { if (err) *err = "已取消"; return -1; }
    if (expected_size > 0 && done < expected_size) {
        if (err) *err = "下载未完成（可续传）";
        return 1; // 未完成 → 上层续传重试
    }
    if (on_progress) on_progress(done, total);
    return 0;
}

int download_file(const DownloadFile& f,
                  const std::function<bool(uint64_t, uint64_t)>& on_progress,
                  std::string* err) {
    const std::vector<std::string> sources = { f.url, f.mirror };

    for (const auto& src : sources) {
        if (src.empty()) continue;
        // 已下载部分（断点续传）
        uint64_t resume_from = 0;
        FILE* fp = fopen(f.path.c_str(), "rb");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            resume_from = (uint64_t)ftell(fp);
            fclose(fp);
        }
        if (f.expected_size > 0 && resume_from >= f.expected_size) {
            return 0; // 已完成
        }

        int rc = http_get_to_file(src, resume_from, f.path, f.expected_size,
                                  on_progress, err, 5);
        if (rc == 0) return 0;
        if (rc == 1) {
            // 未完成 → 重试续传
            if (err) err->clear();
            continue;
        }
        if (err && !err->empty()) {
            fprintf(stderr, "[dl] %s 失败: %s\n", f.path.c_str(), err->c_str());
            err->clear();
        }
        // rc == -1 → 换镜像
    }
    return -1; // 所有源失败
}
