#include "model_downloader.h"

#include <cstdio>
#include <vector>

#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

// WinHTTP 请求 + 断点续传 + 重定向跟随
static bool http_get_range(const std::string& url, uint64_t resume_from,
                           std::vector<char>& out, uint64_t& total_size,
                           bool& server_supports_range,
                           std::string* err) {
    // 解析 URL
    WCHAR host[256] = {}, path[1024] = {};
    URL_COMPONENTSW comp = {};
    comp.dwStructSize = sizeof(comp);
    comp.lpszHostName = host; comp.dwHostNameLength = 256;
    comp.lpszUrlPath = path;  comp.dwUrlPathLength = 1024;
    const std::wstring wurl(url.begin(), url.end());
    if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &comp)) {
        if (err) *err = "URL 解析失败";
        return false;
    }

    HINTERNET ses = WinHttpOpen(L"LiveSub-ModelDownloader/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) { if (err) *err = "WinHTTP 初始化失败"; return false; }
    HINTERNET con = WinHttpConnect(ses, host, comp.nPort, 0);
    if (!con) { WinHttpCloseHandle(ses); if (err) *err = "连接失败"; return false; }

    std::wstring reqpath = (comp.nScheme == INTERNET_SCHEME_HTTPS ? L"https://" : L"http://");
    reqpath += host;
    if (comp.nPort != 80 && comp.nPort != 443) {
        reqpath += L":" + std::to_wstring(comp.nPort);
    }
    reqpath += path;

    HINTERNET req = WinHttpOpenRequest(con, L"GET", reqpath.c_str(), nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       comp.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!req) { WinHttpCloseHandle(con); WinHttpCloseHandle(ses); if (err) *err = "创建请求失败"; return false; }

    if (resume_from > 0) {
        const std::wstring range = L"bytes=" + std::to_wstring(resume_from) + L"-";
        WinHttpAddRequestHeaders(req, range.c_str(), (DWORD)range.size(), WINHTTP_ADDREQ_FLAG_REPLACE | WINHTTP_ADDREQ_FLAG_ADD);
    }
    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
        if (err) *err = "发送请求失败";
        return false;
    }
    if (!WinHttpReceiveResponse(req, nullptr)) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
        if (err) *err = "接收响应失败";
        return false;
    }

    DWORD status = 0, status_len = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_len, WINHTTP_NO_HEADER_INDEX);
    if (status == 302 || status == 301 || status == 307 || status == 308) {
        // 重定向：取 Location 重新请求（简化：最多 5 跳）
        WCHAR loc[2048] = {};
        DWORD loc_len = sizeof(loc);
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                loc, &loc_len, WINHTTP_NO_HEADER_INDEX)) {
            // 递归重试（URL 替换）
            WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
            return http_get_range(std::string(loc, loc + wcslen(loc)), resume_from,
                                  out, total_size, server_supports_range, err);
        }
        if (err) *err = "重定向失败";
        WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
        return false;
    }
    if (status != 200 && status != 206) {
        if (err) *err = "HTTP " + std::to_string(status);
        WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
        return false;
    }

    // Content-Length / Content-Range（断点续传确认）
    total_size = 0;
    WCHAR clen[64] = {};
    DWORD clen_len = sizeof(clen);
    if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                            clen, &clen_len, WINHTTP_NO_HEADER_INDEX)) {
        total_size = _wtoi64(clen);
    }
    server_supports_range = (status == 206);
    if (status == 206 && resume_from > 0 && total_size > 0) {
        total_size += resume_from; // Content-Length 是本次段长
    }

    // 读数据
    char buf[64 * 1024];
    DWORD got = 0;
    while (WinHttpReadData(req, buf, sizeof(buf), &got) && got > 0) {
        out.insert(out.end(), buf, buf + got);
        got = 0;
    }
    WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
    return true;
}

int download_file(const DownloadFile& f,
                  const std::function<bool(uint64_t, uint64_t)>& on_progress,
                  std::string* err) {
    const std::vector<std::string> sources = { f.url, f.mirror };

    for (const auto& src : sources) {
        if (src.empty()) continue;
        uint64_t resume_from = 0;
        // 已下载部分（断点续传）
        FILE* fp = fopen(f.path.c_str(), "rb");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            resume_from = (uint64_t)ftell(fp);
            fclose(fp);
        }
        if (f.expected_size > 0 && resume_from >= f.expected_size) {
            return 0; // 已完成
        }

        std::vector<char> data;
        uint64_t total = 0;
        bool range_ok = false;
        if (!http_get_range(src, resume_from, data, total, range_ok, err)) {
            continue; // 换镜像
        }
        if (!range_ok && resume_from > 0) {
            // 服务器不支持续传 → 重新下载
            resume_from = 0;
            data.clear();
            if (!http_get_range(src, 0, data, total, range_ok, err)) {
                continue;
            }
        }

        // 追加写入
        FILE* w = fopen(f.path.c_str(), resume_from > 0 ? "ab" : "wb");
        if (!w) { if (err) *err = "无法创建文件: " + f.path; return -1; }
        if (resume_from == 0) fseek(w, 0, SEEK_SET);
        fwrite(data.data(), 1, data.size(), w);
        fflush(w);
        const uint64_t done = resume_from + data.size();
        fclose(w);

        // 校验
        if (f.expected_size > 0) {
            if (done < f.expected_size) {
                // 未完成：模拟"中断"，让上层重试续传（此处返回重试标记）
                if (on_progress) on_progress(done, f.expected_size);
                if (err) *err = "下载未完成（可重试续传）";
                return 1;
            }
            // 清理临时文件（无）
        }
        if (on_progress) on_progress(done, total > 0 ? total : done);
        return 0;
    }
    return -1; // 所有源失败
}
