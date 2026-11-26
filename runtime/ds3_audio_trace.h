#ifndef DS3_AUDIO_TRACE_H
#define DS3_AUDIO_TRACE_H
#include <stdint.h>
enum { DAT_CURSOR, DAT_LOCK, DAT_UNLOCK, DAT_FRESH, DAT_BEGIN, DAT_END, DAT_SINK, DAT_CREATE, DAT_RELEASE, DAT_PLAY, DAT_STOP, DAT_SEEK, DAT_MIX_WAIT, DAT_MIX_BEGIN, DAT_MIX_END, DAT_RING_AMBIGUOUS, DAT_RING_LATE };
void ds3_audio_trace_init(void);
void ds3_audio_trace_drain(void);
void ds3_audio_trace_finish(void);
int ds3_audio_trace_enabled(void);
void ds3_audio_trace(int kind, uint64_t id, uint32_t a, uint32_t b, uint32_t c);
#endif
