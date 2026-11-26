/* Dispatch and HLE trace probes; tests/native/run_ds3_runtime.sh checks exit and stderr. */
#include "hle.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void cleaned(void) { fputs("host atexit retained\n", stderr); }
int main(int argc, char** argv) {
    assert(argc == 2);
    rt_init();
    atexit(cleaned);
    if (!strcmp(argv[1], "trace")) {
        hle_call("kernel32.dll", "GetLastError", 0, NULL);
        hle_call("kernel32.dll", "GetACP", 0, NULL);
        return 0;
    }
    if (!strcmp(argv[1], "null")) rt_call(&g_cpu, 0, 0, NULL);
    static Module m = {.name="edge-fixture.dll", .base=0x10000000, .size=4};
    char path[] = "build/edge-fixture-XXXXXX";
    int fd = mkstemp(path); assert(fd >= 0 && write(fd, "\x90\xcc\xc3\x00", 4) == 4); close(fd);
    rt_load_module(&m, path); unlink(path);
    rt_call(&g_cpu, m.base+3, 0, NULL);
    return 99;
}
