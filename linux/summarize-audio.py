#!/usr/bin/env python3
"""Summarize metadata only. A callback gap is not proof of a sink underrun."""
import argparse
import bisect
import collections
import re

LINE = re.compile(r'^\[ds3-audio\] (\d+) (\w+) (\d+) (\d+) (\d+) (\d+)$')


def summarize(lines, start=0, stop=2**64-1):
    rows = []
    for line in lines:
        m = LINE.fullmatch(line.rstrip())
        if m:
            t, kind, i, a, b, c = m.groups()
            if start <= int(t) <= stop:
                rows.append((int(t), kind, int(i), int(a), int(b), int(c)))
    rows.sort()
    counts = collections.Counter(k for _, k, *_ in rows)
    late, shortage, callback, mixer, lock = [], [], [], [], []
    frame_request = prev = None
    lock_start = mix_start = None
    for t, k, i, a, b, c in rows:
        if k == 'callback_begin':
            if prev and prev[2] and t-prev[0] > 1.5 * prev[1]*1e9/prev[2]:
                late.append((prev[0], t))
            prev = frame_request = (t,a,b)
        elif k == 'callback_end' and frame_request:
            callback.append(t-frame_request[0])
            frame_request = None
        elif k == 'mix_lock_wait':
            lock_start, mix_start = t, None
        elif k == 'mix_begin':
            if lock_start is not None:
                lock.append(t-lock_start)
            lock_start, mix_start = None, t
        elif k == 'mix_end' and mix_start is not None:
            mixer.append(t-mix_start)
            mix_start = None
        elif k == 'fresh' and c < a:
            shortage.append(t)
    gaps = [end for _, end in late]
    def adjacent(t):
        p = bisect.bisect_left(gaps,t)
        return any(abs(t-gaps[i]) <= 30_000_000 for i in (p-1,p) if 0 <= i < len(gaps))
    both = sum(map(adjacent,shortage))
    return {'events':len(rows), 'fresh_shortage':len(shortage), 'callback_gaps':len(late),
            'shortage_near_gap_ambiguous':both, 'max_callback_ms':max(callback,default=0)/1e6,
            'max_mixer_ms':max(mixer,default=0)/1e6, 'max_mixer_lock_wait_ms':max(lock,default=0)/1e6,
            'sink_depth':'unavailable' if counts['sink_depth_unavailable'] else 'not observed',
            'dropped':sum(a for _,k,_,a,_,_ in rows if k=='dropped'), 'unlocks':counts['unlock']}


if __name__ == '__main__':
    cli=argparse.ArgumentParser(description=__doc__)
    cli.add_argument('log'); cli.add_argument('--start-ns',type=int,default=0); cli.add_argument('--stop-ns',type=int,default=2**64-1)
    args=cli.parse_args()
    with open(args.log,encoding='utf-8',errors='replace') as f:
        for name,value in summarize(f,args.start_ns,args.stop_ns).items():
            print(f'{name}: {value}')
