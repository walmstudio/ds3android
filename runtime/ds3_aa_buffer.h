/* AAudio output capacity for the in-callback OpenAL/DirectSound mixer.
 *
 * This is not the WOLF PROJECT 0.7 AAudio write-retry path: DS3 never calls
 * AAudioStream_write. The mixer runs inside the AAudio data callback. The
 * comparable resilience knob is stream capacity. WOLF 0.7 deepened its queue
 * from 50 ms to about 90 ms; the previous DS3 floor was rate/25 (40 ms) with
 * four bursts. Keep PERFORMANCE_MODE_NONE: LOW_LATENCY shrinks the buffer and
 * is the wrong direction for a heavy in-callback mix.
 */
#ifndef DS3_AA_BUFFER_H
#define DS3_AA_BUFFER_H

static inline int ds3_aa_buffer_frames(int rate, int burst)
{
    if (rate <= 0 || burst <= 0) return 0;
    int want = burst * 4, floor = rate / 11; /* 4 bursts, >= ~90 ms */
    while (want < floor) want += burst;
    return want;
}

#endif
