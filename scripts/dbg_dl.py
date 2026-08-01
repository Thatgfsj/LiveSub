# -*- coding: utf-8 -*-
# 临时调试：http_get_range_impl 各阶段日志
p = r'src\tools\model_downloader.cpp'
s = open(p, encoding='utf-8').read()

old = '''    if (status == 302 || status == 301 || status == 307 || status == 308) {'''
new = '''    fprintf(stderr, "[dbg] url=%s status=%lu resume=%llu\\n", url.c_str(), (unsigned long)status, (unsigned long long)resume_from);
    if (status == 302 || status == 301 || status == 307 || status == 308) {'''
assert old in s, 'status dbg'
s = s.replace(old, new)

old = '''        WCHAR loc[2048] = {};
        DWORD loc_len = sizeof(loc);
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                loc, &loc_len, WINHTTP_NO_HEADER_INDEX)) {'''
new = '''        WCHAR loc[2048] = {};
        DWORD loc_len = sizeof(loc);
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                loc, &loc_len, WINHTTP_NO_HEADER_INDEX)) {
            fprintf(stderr, "[dbg] redirect -> %ls\\n", loc);'''
assert old in s, 'loc dbg'
s = s.replace(old, new)

open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('dbg OK')
