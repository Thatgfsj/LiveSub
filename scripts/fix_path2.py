# -*- coding: utf-8 -*-
# 修复 main.cpp 路径分隔符（文件里是单反斜杠 "\Q → 需改为源码双反斜杠 "\\Q）
import re
p = r'src\main.cpp'
s = open(p, encoding='utf-8').read()

# 匹配 mp + "\Qwen3... （单反斜杠）→ 替换为 mp + "\\Qwen3... （源码双反斜杠，编译后单反斜杠）
def fix(m):
    return m.group(1) + '"\\\\' + m.group(2)
s2, n1 = re.subn(r'(mp \+ ")(\Qwen3-ASR-1\.7B-Q8_0\.gguf")', fix, s)
print('main_f:', n1)
s2, n2 = re.subn(r'(mp \+ ")(\Qwen3-ASR-0\.6B-Q8_0\.gguf")', fix, s2)
print('sml_f:', n2)
s2, n3 = re.subn(r'(mp \+ ")(\mmproj-Qwen3-ASR-1\.7B-bf16\.gguf")', fix, s2)
print('proj_f:', n3)
assert n1 + n2 + n3 == 3
open(p, 'w', encoding='utf-8', newline='\n').write(s2)
print('OK')
