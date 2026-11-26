#!/usr/bin/env python3
"""Host fixture for desktop and Android registry-directory policy."""
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    reg = Path(__file__).with_name('hle_reg.inc').read_text()
    start = reg.index('static int reg_profile_dir(')
    body = reg[start:reg.index('\nstatic int reg_tr(', start)]
    code = '''
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static const char* game;
static const char* hle_fs_game_dir(void) { return game; }
''' + body + '''
int main(int argc, char** argv) {
    assert(argc == 5);
    game = argv[1];
    assert(reg_profile_dir(game) < 0);
    int fd = reg_profile_dir(argv[2]);
#ifdef __ANDROID__
    assert(fd >= 0 && "Android must admit exact game/Documents"); close(fd);
#else
    assert(fd < 0);
#endif
    fd = reg_profile_dir(argv[3]); assert(fd >= 0); close(fd);
    assert(reg_profile_dir(argv[4]) < 0);
    return 0;
}
'''
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        game = root / 'game'
        game.mkdir()
        docs = game / 'Documents'
        profile = root / 'other-profile'
        nested = game / 'not-Documents'
        source = root / 'fixture.c'
        source.write_text(code)
        for android in (False, True):
            exe = root / 'fixture'
            flags = ['-D__ANDROID__'] if android else []
            subprocess.run([os.environ.get('CC', 'cc'), *flags, str(source), '-o', str(exe)], check=True)
            subprocess.run([exe, game, docs, profile, nested], check=True)
    print('PASS: shared registry profile policy (desktop and Android modes)')


if __name__ == '__main__':
    main()
