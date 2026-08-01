# -*- coding: utf-8 -*-
# 修正：group(2) 去掉原反斜杠，输出源码双反斜杠
import re
p = r'src\main.cpp'
s = open(p, encoding='utf-8').read()

def fix(m):
    return m.group(1) + '\\\\' + m.group(2)[1:]  # \\ + Qwen3... → "\\Qwen3

pat1 = re.compile(r'(mp \+ ")(\\Qwen3-ASR-1\.7B-Q8_0\.gguf")')
s, n1 = pat1.subn(fix, s)
pat2 = re.compile(r'(mp \+ ")(\\Qwen3-ASR-0\.6B-Q8_0\.gguf")')
s, n2 = pat2.subn(fix, s)
pat3 = re.compile(r'(mp \+ ")(\\mmproj-Qwen3-ASR-1\.7B-bf16\.gguf")')
s, n3 = pat3.subn(fix, s)
print(n1, n2, n3)
assert n1 + n2 + n3 == 3
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('OK')
