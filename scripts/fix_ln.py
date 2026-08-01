# -*- coding: utf-8 -*-
# 修复 L'\n' 被 heredoc 破坏
p = r'src\ui\subtitle_window.cpp'
s = open(p, encoding='utf-8').read()

import re
# 匹配损坏的 multi_line_text 行：L' 后跟真实换行再跟 '
pat = re.compile(r"const bool multi_line_text = text\.find\(L'\n'\) != std::wstring::npos;")
m = pat.search(s)
if m:
    fixed = "const bool multi_line_text = text.find(L'\\n') != std::wstring::npos;"
    s = pat.sub(lambda x: fixed, s)
    open(p, 'w', encoding='utf-8', newline='\n').write(s)
    print('已修复 L\\'\\n\\' 行')
else:
    i = s.find('multi_line_text')
    print('未匹配，实际内容:', repr(s[i-20:i+80]) if i != -1 else 'not found')
