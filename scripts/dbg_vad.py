# -*- coding: utf-8 -*-
# wav 测试：每 200ms 打印 vad speaking 状态
p = r'src\app.cpp'
s = open(p, encoding='utf-8').read()
old = '''    const size_t block = (size_t)target * 20 / 1000;
    size_t off = 0;
    while (off < pcm16.size() && asr_running_) {
        const size_t n = std::min(block, pcm16.size() - off);
        if (mic_.vad) mic_.vad->process(pcm16.data() + off, n, 0);
        if (mic_.queue) mic_.queue->push(pcm16.data() + off, n);
        off += n;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }'''
new = '''    const size_t block = (size_t)target * 20 / 1000;
    size_t off = 0;
    int dbg_n = 0;
    while (off < pcm16.size() && asr_running_) {
        const size_t n = std::min(block, pcm16.size() - off);
        if (mic_.vad) mic_.vad->process(pcm16.data() + off, n, 0);
        if (mic_.queue) mic_.queue->push(pcm16.data() + off, n);
        off += n;
        if (++dbg_n % 10 == 0) {
            logf("[dbg] wav vad speaking=%d que=%zu\\n",
                 (int)mic_.speaking.load(),
                 mic_.queue ? mic_.queue->total_samples() : 0);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }'''
assert old in s, 'wav dbg'
s = s.replace(old, new)
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('OK')
