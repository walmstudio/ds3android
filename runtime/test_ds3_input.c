/* Host fixture for the guest stdcall ABI, including a module-backed callback. */
#include "hle.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define INVALID 0x80070057u
#define OOM 0x8007000eu
#define CALLBACK 0x10000000u
static const uint32_t iid[][4] = {
    {0xbf798030, 0x4da2483a, 0x645d99aa, 0x009736ed},
    {0xbf798031, 0x4da2483a, 0x645d99aa, 0x009736ed},
    {0x54d41080, 0x4833dc15, 0x8f741ba4, 0x7981a373},
    {0x54d41081, 0x4833dc15, 0x8f741ba4, 0x7981a373},
};
static const uint32_t sysmouse[] = {0x6f1d2b60, 0x11cfd5a0, 0x4544c7bf, 0x00005453};
static const uint32_t syskey[] = {0x6f1d2b61, 0x11cfd5a0, 0x4544c7bf, 0x00005453};
static int fail_alloc;
uint32_t __real_ghalloc(uint32_t, int);
uint32_t __wrap_ghalloc(uint32_t n, int zero) { return fail_alloc ? 0 : __real_ghalloc(n, zero); }

static uint32_t invoke(CPU* c, uint32_t fn, int n, const uint32_t* args) {
    uint32_t sp = c->esp;
    for (int i = n - 1; i >= 0; i--) push32(c, args[i]);
    push32(c, 0xfffffff0);
    dispatch_call(c, fn);
    assert(c->esp == sp);
    return c->eax;
}
#define CALL(c, fn, ...) invoke(c, fn, sizeof((uint32_t[]){__VA_ARGS__})/4, (uint32_t[]){__VA_ARGS__})
#define COM(c, self, slot, ...) CALL(c, rd32(rd32(self)+4*(slot)), self, __VA_ARGS__)
static uint32_t obj[2], out[2], info[2], ids[4], dev[2];
static unsigned count[2], wide, stop, nest, first;
static unsigned bytes(unsigned w) { return w ? 1100 : 580; }
static void callback(CPU* c) {
    uint32_t p = ARG(0), ctx = ARG(1);
    assert(rd32(c->esp) == 0xfffffff0 && ctx <= 1);
    unsigned w = ctx ? 1 : wide, size = bytes(w), type = (ctx ? 1 : first + count[ctx]);
    assert(type <= 1 && rd32(p) == size);
    const uint32_t* guid = type ? syskey : sysmouse;
    assert(!memcmp(M+p+4, guid, 16) && !memcmp(M+p+20, guid, 16));
    assert(rd32(p+36) == (type ? 0x413 : 0x212));
    const char* name = type ? "Keyboard" : "Mouse";
    for (unsigned j = 0; j < 2; j++) {
        uint32_t s = p+40+j*(w ? 520 : 260);
        for (unsigned k = 0; k <= strlen(name); k++)
            assert((w ? rd16(s+2*k) : rd8(s+k)) == (unsigned char)name[k]);
    }
    assert(rd32(p+size-4) == (type ? 0x00060001 : 0x00020001));
    assert(!COM(c, obj[w], 3, p+4, out[ctx], 0));
    dev[w] = rd32(out[ctx]); assert(dev[w]);
    uint32_t b = info[ctx]; memset(M+b, 0xab, 1104); wr32(b, size);
    assert(!COM(c, dev[w], 15, b));
    assert(!memcmp(M+p, M+b, size) && rd8(b+size) == 0xab);
    wr32(b, size-20);
    assert(!COM(c, dev[w], 15, b) && !memcmp(M+b+4, M+p+4, size-24));
    wr32(b, size+1); wr8(b+size, 0xab);
    assert(COM(c, dev[w], 15, b) == INVALID && rd8(b+size) == 0xab);
    wr32(b, 44);
    assert(COM(c, dev[w], 15, b) == INVALID && rd32(b) == 44);
    if (!ctx && nest && !count[0]) {
        assert(!COM(c, obj[1], 4, 3, CALLBACK, 1, 1));
        assert(rd32(p) == size && !memcmp(M+p+4, guid, 16));
    }
    count[ctx]++;
    c->eax = ctx || !stop;
    c->esp += 12;
}
int main(void) {
    rt_init();
    static const FuncEntry funcs[] = {{CALLBACK, callback, 0}};
    static Module mod = {.name="input-fixture.dll", .base=CALLBACK, .size=64, .funcs=funcs, .nfuncs=1};
    char path[] = "build/input-fixture-XXXXXX";
    int fd = mkstemp(path); assert(fd >= 0 && !ftruncate(fd, mod.size)); close(fd);
    rt_load_module(&mod, path); unlink(path);
    uint32_t create = hle_resolve_impl("dinput8.dll", "DirectInput8Create"); assert(create);
    for (unsigned i = 0; i < 4; i++) { ids[i] = ghalloc(16, 0); memcpy(M+ids[i], iid[i], 16); }
    for (unsigned i = 0; i < 2; i++) { out[i] = ghalloc(4, 1); info[i] = ghalloc(1104, 1); }
    fail_alloc = 1;
    uint32_t oom = CALL(&g_cpu, create, 0, 0x800, ids[0], out[0], 0);
    assert(rd32(out[0]) == 0);
    fail_alloc = 0;
    for (unsigned i = 0; i < 2; i++) {
        assert(!CALL(&g_cpu, create, 0, 0x800, ids[i], out[i], 0));
        obj[i] = rd32(out[i]); assert(obj[i]);
    }
    uint32_t guid = ghalloc(16, 0); memcpy(M+guid, sysmouse, 16);
    fail_alloc = 1;
    uint32_t device_oom = COM(&g_cpu, obj[0], 3, guid, out[0], 0);
    fail_alloc = 0;
    nest = 1;
    assert(!COM(&g_cpu, obj[0], 4, 0, CALLBACK, 0, 1));
    assert(count[0] == 2 && count[1] == 1); /* baseline must fail: no callbacks */
    assert(oom == OOM && device_oom == OOM && obj[0] != obj[1] && dev[0] != dev[1]);
    assert(!COM(&g_cpu, obj[0], 0, ids[1], out[0]) && rd32(out[0]) == obj[1]);
    assert(!COM(&g_cpu, dev[0], 0, ids[3], out[0]) && rd32(out[0]) == dev[1]);
    count[0] = nest = 0; stop = 1;
    assert(!COM(&g_cpu, obj[0], 4, 0, CALLBACK, 0, 0) && count[0] == 1);
    count[0] = stop = 0; wide = first = 1;
    assert(!COM(&g_cpu, obj[1], 4, 3, CALLBACK, 0, 1) && count[0] == 1);
    count[0] = first = 0;
    assert(!COM(&g_cpu, obj[1], 4, 2, CALLBACK, 0, 1) && count[0] == 1);
    count[0] = 0;
    assert(!COM(&g_cpu, obj[1], 4, 4, CALLBACK, 0, 1) && count[0] == 0);
    assert(!COM(&g_cpu, obj[1], 4, 0, CALLBACK, 0, 0x100) && count[0] == 0);
    assert(COM(&g_cpu, obj[1], 4, 0, 0, 0, 1) == INVALID);
    assert(COM(&g_cpu, obj[1], 4, 0, CALLBACK, 0, 2) == INVALID);
    fail_alloc = 1;
    assert(COM(&g_cpu, obj[1], 4, 0, CALLBACK, 0, 1) == OOM && !count[0]);
    fail_alloc = 0;
    puts("DirectInput A/W callback, nested enumeration, filters, STOP, metadata, bounds and OOM passed");
}
