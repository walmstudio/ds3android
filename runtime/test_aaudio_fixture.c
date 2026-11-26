/* Host-only AAudio output boundary: expose one stream and inject disconnects.
 * Lock fixture state across the worker/device boundary; never call while locked. */
#include <stdint.h>
#include <pthread.h>
#include <time.h>
typedef struct Stream Stream;
typedef int32_t (*Data)(Stream*,void*,void*,int32_t);
typedef void (*Error)(Stream*,void*,int32_t);
typedef struct { Data data; Error error; void *du, *eu; } Builder;
struct Stream { Builder b; int rate; };
static Builder builder;
static Stream stream;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int started, opens, fail_open, fail_start, next_rate = 44100;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static int held, entered, timed_out, stops;
void fixture_hold(int op) { pthread_mutex_lock(&lock); held=op; entered=timed_out=0; pthread_cond_broadcast(&cv); pthread_mutex_unlock(&lock); }
void fixture_release(void) { pthread_mutex_lock(&lock); held=0; pthread_cond_broadcast(&cv); pthread_mutex_unlock(&lock); }
int fixture_entered(void) { pthread_mutex_lock(&lock); int n=entered; pthread_mutex_unlock(&lock); return n; }
int fixture_timed_out(void) { pthread_mutex_lock(&lock); int n=timed_out; pthread_mutex_unlock(&lock); return n; }
int fixture_stops(void) { pthread_mutex_lock(&lock); int n=stops; pthread_mutex_unlock(&lock); return n; }
static void block_call(int op) {
    if (held != op) return;
    struct timespec until; clock_gettime(CLOCK_REALTIME, &until); until.tv_sec += 2;
    entered = op;
    while (held == op && !timed_out) timed_out = pthread_cond_timedwait(&cv, &lock, &until) != 0;
    if (timed_out) held = 0;
}
void fixture_fail_open(void) { pthread_mutex_lock(&lock); fail_open=1; pthread_mutex_unlock(&lock); }
void fixture_fail_start(void) { pthread_mutex_lock(&lock); fail_start=1; pthread_mutex_unlock(&lock); }
void fixture_rate(int rate) { pthread_mutex_lock(&lock); next_rate=rate; pthread_mutex_unlock(&lock); }
int32_t AAudio_createStreamBuilder(Builder** b) { *b=&builder; return 0; }
void AAudioStreamBuilder_setFormat(Builder* b,int32_t n) { (void)b; (void)n; }
void AAudioStreamBuilder_setChannelCount(Builder* b,int32_t n) { (void)b; (void)n; }
void AAudioStreamBuilder_setDataCallback(Builder* b,Data cb,void* u) { b->data=cb; b->du=u; }
void AAudioStreamBuilder_setErrorCallback(Builder* b,Error cb,void* u) { b->error=cb; b->eu=u; }
int32_t AAudioStreamBuilder_openStream(Builder* b,Stream** s) {
    pthread_mutex_lock(&lock);
    block_call(1);
    if (fail_open) { pthread_mutex_unlock(&lock); return -899; }
    stream.b=*b; stream.rate=next_rate; *s=&stream; pthread_mutex_unlock(&lock); return 0;
}
int32_t AAudioStreamBuilder_delete(Builder* b) { (void)b; return 0; }
int32_t AAudioStream_requestStart(Stream* s) {
    (void)s; pthread_mutex_lock(&lock); block_call(3);
    if (fail_start) { pthread_mutex_unlock(&lock); return -899; }
    started=1; ++opens; pthread_mutex_unlock(&lock); return 0;
}
int32_t AAudioStream_requestStop(Stream* s) {
    (void)s; pthread_mutex_lock(&lock); block_call(2); started=0; ++stops; pthread_mutex_unlock(&lock); return 0;
}
int32_t AAudioStream_close(Stream* s) { (void)s; return 0; }
int32_t AAudioStream_getSampleRate(Stream* s) { return s->rate; }
int fixture_opens(void) { pthread_mutex_lock(&lock); int n=opens; pthread_mutex_unlock(&lock); return n; }
void fixture_lose(void) {
    pthread_mutex_lock(&lock); int on=started; started=0; Builder b=stream.b; pthread_mutex_unlock(&lock);
    if (on) b.error(&stream,b.eu,-899);
}
void fixture_pull(int16_t* out,int32_t n) {
    pthread_mutex_lock(&lock); int on=started || (held==3 && entered==3); Builder b=stream.b; pthread_mutex_unlock(&lock);
    if (on) b.data(&stream,b.du,out,n); else for (int i=0; i<2*n; ++i) out[i]=0;
}
