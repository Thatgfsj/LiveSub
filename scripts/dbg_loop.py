# -*- coding: utf-8 -*-
# asr_loop 心跳日志
p = r'src\app.cpp'
s = open(p, encoding='utf-8').read()
old = '''void App::asr_loop() {
    while (asr_running_) {
        bool worked = false;
        if (mic_.enabled.load() || mic_.finalize_pending.load()) worked |= process_pipeline(mic_);'''
new = '''void App::asr_loop() {
    int64_t last_hb = 0;
    while (asr_running_) {
        const int64_t hb = now_ms();
        if (hb - last_hb > 500) {
            last_hb = hb;
            logf("[dbg] loop mic=%d pc=%d\\n", (int)mic_.enabled.load(), (int)pc_.enabled.load());
        }
        bool worked = false;
        if (mic_.enabled.load() || mic_.finalize_pending.load()) worked |= process_pipeline(mic_);'''
assert old in s, 'hb'
s = s.replace(old, new)
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('OK')
