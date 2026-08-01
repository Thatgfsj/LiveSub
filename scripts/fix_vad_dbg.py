# -*- coding: utf-8 -*-
# 修复 vad.cpp 被破坏的 dbg 行
p = r'src\asr\vad.cpp'
s = open(p, encoding='utf-8').read()
import re
pat = re.compile(r"static int dbg_n = 0;\n    if \(\+\+dbg_n % 25 == 0\) \{\n        fprintf\(stderr, \"\[vad\] db=%.1f th=%.1f in_speech=%d\n\", db, threshold, \(int\)in_speech_\);\n    \}")
m = pat.search(s)
if m:
    fixed = '    static int dbg_n = 0;\n    if (++dbg_n % 25 == 0) {\n        fprintf(stderr, "[vad] db=%.1f th=%.1f in_speech=%d\\n", db, threshold, (int)in_speech_);\n    }'
    s = pat.sub(lambda x: fixed, s)
    open(p, 'w', encoding='utf-8', newline='\n').write(s)
    print('fixed')
else:
    i = s.find('[vad] db=')
    print('not matched:', repr(s[i-80:i+120]) if i != -1 else 'nf')
