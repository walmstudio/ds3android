#include "hle.h"
#include <assert.h>
#include <stdio.h>

extern void (*ds3_desktop_mode_hook)(unsigned,unsigned,int) __attribute__((weak));
static unsigned calls, last_w, last_h;
static int last_full;
static void mode_changed(unsigned w,unsigned h,int full) {
    ++calls; last_w=w; last_h=h; last_full=full;
}
static uint32_t method(uint32_t obj,unsigned slot) { return rd32(rd32(obj)+4*slot); }
#define CALL(fn, ...) rt_call(&g_cpu, (fn), sizeof((uint32_t[]){__VA_ARGS__})/4, (uint32_t[]){__VA_ARGS__})
static void set_pp(uint32_t pp,unsigned w,unsigned h,int windowed) {
    wr32(pp,w); wr32(pp+4,h); wr32(pp+8,22); wr32(pp+12,1); wr32(pp+24,1); wr32(pp+32,windowed);
}
int main(void) {
    rt_init();
    if (&ds3_desktop_mode_hook) ds3_desktop_mode_hook=mode_changed;
    uint32_t out=ghalloc(4,1), pp=ghalloc(56,1);
    uint32_t d3d=CALL(hle_resolve_impl("d3d9.dll","Direct3DCreate9"),32);
    set_pp(pp,1280,720,1);
    assert(!CALL(method(d3d,16),d3d,0,1,0,0x40,pp,out));
    if (!calls) { fputs("CreateDevice did not publish mode\n",stderr); return 1; }
    assert(calls==1 && last_w==1280 && last_h==720 && last_full==0);
    uint32_t dev=rd32(out);
    set_pp(pp,1600,900,0);
    assert(!CALL(method(dev,16),dev,pp));
    assert(calls==2 && last_w==1600 && last_h==900 && last_full==1);
    assert(CALL(method(dev,16),dev,0));
    assert(calls==2);
    set_pp(pp,1024,768,1);
    assert(!CALL(method(dev,16),dev,pp));
    assert(calls==3 && last_w==1024 && last_h==768 && last_full==0);
    puts("PASS guest CreateDevice/Reset publishes actual backbuffer dimensions and mode exactly once");
}
