import pathlib
import re
import sys


def native_audio_ok(log, backend):
    tag = rf'\[ds3-{re.escape(backend)}\]'
    selected = re.findall(r'^\[ds3-audio-out\] selected=(\w+) rate=\d+$', log, re.M)
    summary = re.findall(rf'^{tag} submitted=([1-9]\d*)[^\n]* status=(\w+) age_ms=(\d+)$', log, re.M)
    return (selected == [backend] and len(summary) == 1 and summary[0][1] == 'ok'
            and int(summary[0][2]) <= 2000 and not re.search(rf'^{tag} .*output stopped', log, re.M))


if __name__ == '__main__':
    if not native_audio_ok(pathlib.Path(sys.argv[1]).read_text(errors='replace'), sys.argv[2]):
        raise SystemExit('native audio gate: no healthy, recent submission from expected backend')
