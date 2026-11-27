#include "ds3_aa_buffer.h"
#include <stdio.h>

static int fail;

static void expect(int rate, int burst, int want, const char *label)
{
    int got = ds3_aa_buffer_frames(rate, burst);
    if (got != want) {
        fprintf(stderr, "FAIL %s: rate=%d burst=%d expected %d got %d\n", label, rate, burst, want, got);
        fail = 1;
    }
}

int main(void)
{
    expect(0, 192, 0, "invalid rate");
    expect(48000, 0, 0, "invalid burst");
    /* 20 ms burst: 4*960=3840 < 48000/11=4363, grow by one burst -> 4800 = 100 ms */
    expect(48000, 960, 4800, "48 kHz 20 ms burst reaches 90 ms floor");
    /* 4 ms burst grows to the first multiple of 192 at or above 4363 -> 4416 */
    expect(48000, 192, 4416, "48 kHz 4 ms burst");
    /* 40 ms burst: 4*1920=7680 already above the floor (160 ms) */
    expect(48000, 1920, 7680, "48 kHz 40 ms burst keeps four bursts");
    /* old floor rate/25 at 48 kHz was 1920; 90 ms floor must be strictly larger */
    if (ds3_aa_buffer_frames(48000, 192) <= 48000 / 25) {
        fprintf(stderr, "FAIL new floor is not deeper than the old 40 ms floor\n");
        fail = 1;
    }
    if (!fail) fprintf(stderr, "test_aa_buffer: ok\n");
    return fail;
}
