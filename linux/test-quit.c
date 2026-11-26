#include "hle.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WNDPROC 0x10000000u
static uint32_t hwnd, msg, destroy_count, mode;
static uint32_t call(CPU *c, const char *name, int n, uint32_t *a) {
    uint32_t fn=hle_resolve_impl("user32.dll",name), sp=c->esp;
    assert(fn);
    uint32_t r=rt_call(c,fn,n,a);
    assert(c->esp==sp);
    return r;
}
#define U(c,n,...) call(c,n,sizeof((uint32_t[]){__VA_ARGS__})/4,(uint32_t[]){__VA_ARGS__})
static void wndproc(CPU *c) {
    assert(ARG(0)==hwnd);
    if (ARG(1)==0x10) assert(U(c,"PostMessageA",hwnd,0x464,6,0));
    if (ARG(1)==0x464) { assert(ARG(2)==6); assert(U(c,"DestroyWindow",hwnd)); }
    if (ARG(1)==2) {
        ++destroy_count;
        assert(U(c,"IsWindow",hwnd));
        assert(!U(c,"DestroyWindow",hwnd));
        U(c,"PostQuitMessage",77);
    }
    c->eax=0;
    c->esp+=20;
}
static void *other(void *unused) {
    (void)unused;
    CPU c=g_cpu;
    c.fs_base=ghalloc(4096,1);
    wr32(c.fs_base+0x24,1234);
    c.esp=ghalloc(4096,1)+4096-16;
    assert(!U(&c,"PeekMessageW",msg,0,0x999,0x999,1));
    U(&c,"PostQuitMessage",31);
    assert(U(&c,"PeekMessageW",msg,0,0x999,0x999,1));
    assert(rd32(msg+8)==31);
    return NULL;
}
int main(int argc, char **argv) {
    (void)argv;
    mode=argc>1;
    rt_init();
    static const FuncEntry fn[]={{WNDPROC,wndproc,0}};
    static Module mod={.name="quit-fixture.dll",.base=WNDPROC,.size=64,.funcs=fn,.nfuncs=1};
    char path[]="build/quit-fixture-XXXXXX";
    int fd=mkstemp(path); assert(fd>=0 && !ftruncate(fd,mod.size)); close(fd);
    rt_load_module(&mod,path); unlink(path);
    uint32_t cls=ghalloc(40,1), name=ghalloc(16,1);
    memcpy(M+name,"quit fixture",13); wr32(cls+4,WNDPROC); wr32(cls+36,name);
    assert(U(&g_cpu,"RegisterClassA",cls));
    hwnd=U(&g_cpu,"CreateWindowExA",0,name,0,0x10000000,0,0,640,480,0,0,0,0);
    assert(hwnd); msg=ghalloc(28,1);
    if (mode) {
        assert(hle_ui_close());
        assert(U(&g_cpu,"PeekMessageW",msg,hwnd,0x10,0x10,1));
        U(&g_cpu,"DispatchMessageW",msg);
        assert(U(&g_cpu,"PeekMessageW",msg,hwnd,0x464,0x464,1));
        U(&g_cpu,"DispatchMessageW",msg);
    } else assert(U(&g_cpu,"DestroyWindow",hwnd));
    if (destroy_count!=1) { fputs("DestroyWindow skipped WM_DESTROY\n",stderr); return 1; }
    assert(!U(&g_cpu,"IsWindow",hwnd));
    pthread_t t; assert(!pthread_create(&t,NULL,other,NULL) && !pthread_join(t,NULL));
    assert(U(&g_cpu,"PeekMessageW",msg,0,0x999,0x999,0));
    assert(rd32(msg)==0 && rd32(msg+4)==0x12 && rd32(msg+8)==77 && rd32(msg+12)==0);
    assert(U(&g_cpu,"PeekMessageW",msg,0,0x999,0x999,1));
    assert(!U(&g_cpu,"PeekMessageW",msg,0,0x999,0x999,1));
    puts("PASS owner WM_DESTROY and thread-local WM_QUIT");
}
