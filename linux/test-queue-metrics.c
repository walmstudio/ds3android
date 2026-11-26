#include "hle_d3d9_cmd.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

static unsigned count;
static void exec_record(CmdHdr* h) { assert(h->type==9); ++count; }
static void idle(int ev) { (void)ev; }
int main(void) {
    const unsigned cap=1<<20, bytes=131072;
    uint8_t* ring=calloc(1,cap);
    assert(cmd_init(1,ring,0,cap,exec_record,idle));
    uint64_t pos=0;
    for (int i=0;i<9;i++) { void* p=cmd_begin(bytes); assert(p); pos=cmd_end(p,bytes,9,"fixture"); }
#ifdef QUEUE_METRICS_AVAILABLE
    cmd_trace_wait_present(pos);
#else
    cmd_wait(pos);
#endif
    assert(count==9 && cmd_done()==pos);
    printf("order=%016llx\n",(unsigned long long)cmd_order_hash());
#ifdef QUEUE_METRICS_AVAILABLE
    cmd_trace_report();
#endif
}
