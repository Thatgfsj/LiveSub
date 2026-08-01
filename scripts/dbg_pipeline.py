# -*- coding: utf-8 -*-
# 临时调试：process_pipeline 各阶段日志
p = r'src\app.cpp'
s = open(p, encoding='utf-8').read()

old = '''bool App::process_pipeline(AsrPipeline& p) {
    const int64_t t_now = now_ms();
    const bool finalize = p.finalize_pending.exchange(false);
    if (!p.enabled.load() || !p.queue) {
        if (finalize) p.finalize_pending = true; // 恢复标志，稍后处理
        return false;
    }
    if (!p.speaking.load() && !finalize) {
        last_asr_heartbeat_ms_ = t_now;
        return false;
    }'''
new = '''bool App::process_pipeline(AsrPipeline& p) {
    const int64_t t_now = now_ms();
    const bool finalize = p.finalize_pending.exchange(false);
    if (!p.enabled.load() || !p.queue) {
        if (finalize) p.finalize_pending = true; // 恢复标志，稍后处理
        return false;
    }
    if (!p.speaking.load() && !finalize) {
        last_asr_heartbeat_ms_ = t_now;
        return false;
    }
    logf("[dbg] %s: enter spe=%d fin=%d\\n", p.name.c_str(), (int)p.speaking.load(), (int)finalize);'''
assert old in s, 'dbg1'
s = s.replace(old, new)

old = '''    // 取窗口（finalize 用固定段尾，上限 30s；部分结果 8s）
    const size_t seg_end = finalize ? p.seg_end.load() : 0;
    const size_t max_len = (size_t)asr_.sample_rate() * (finalize ? 30 : 8);
    const size_t n = p.queue->take_segment(seg_now, seg_end, max_len, p.win_buf);
    if (n == 0) return false;'''
new = '''    // 取窗口（finalize 用固定段尾，上限 30s；部分结果 8s）
    const size_t seg_end = finalize ? p.seg_end.load() : 0;
    const size_t max_len = (size_t)asr_.sample_rate() * (finalize ? 30 : 8);
    const size_t n = p.queue->take_segment(seg_now, seg_end, max_len, p.win_buf);
    logf("[dbg] %s: total=%zu seg=%zu hop=%zu n=%zu\\n", p.name.c_str(),
         total_now, seg_now, hop_samples, n);
    if (n == 0) return false;'''
assert old in s, 'dbg2'
s = s.replace(old, new)

open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('dbg OK')
