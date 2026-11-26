#include "hle.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t api(const char* name, unsigned n, uint32_t* args) {
    uint32_t f=hle_resolve_impl("advapi32.dll",name);
    assert(f);
    return rt_call(&g_cpu,f,n,args);
}
#define R(n,...) api(n,sizeof((uint32_t[]){__VA_ARGS__})/4,(uint32_t[]){__VA_ARGS__})
int main(int argc,char** argv) {
    assert(argc==5);
    rt_init(); hle_fs_layout("C:\\DeadSpace3","C:\\DeadSpace3",1);
    assert(hle_fs_init(argv[1],argv[2],argv[3]));
    uint32_t p=ghalloc(128,1), h, path=gstrdup("Software\\DS3Fixture"), wpath=hle_wstr("software\\ds3fixture");
    uint32_t lang=gstrdup("Locale"), wlang=hle_wstr("LOCALE"), langval=hle_wstr("ru_RU");
    uint32_t rc=R("RegOpenKeyExA",0x80000001u,path,0u,0x20019u,p);
    if (!strcmp(argv[4],"empty")) { assert(rc==2); puts("PASS registry isolated"); return 0; }
    if (!strcmp(argv[4],"blocked")) {
        uint32_t name=gstrdup("DS3RootFixture"); wr32(p+32,42);
        assert(R("RegSetValueExA",0x80000001u,name,0u,4u,p+32,4u)==5);
        wr32(p+4,4);
        assert(R("RegQueryValueExA",0x80000001u,name,0u,p+8,p+24,p+4)==2);
        assert(R("RegCreateKeyExW",0x80000001u,wpath,0u,0u,0u,0x2001fu,0u,p,p+4)==5);
        assert(R("RegOpenKeyExA",0x80000001u,path,0u,0x20019u,p)==2);
        puts("PASS failed persistent operations are not visible"); return 0;
    }
    if (!strcmp(argv[4],"write")) {
        assert(rc==2);
        assert(!R("RegCreateKeyExW",0x80000001u,wpath,0u,0u,0u,0x2001fu,0u,p,p+4) && rd32(p+4)==1);
        h=rd32(p);
        assert(!R("RegSetValueExW",h,wlang,0u,1u,langval,12u));
        wr32(p+32,42);
        assert(!R("RegSetValueExA",h,gstrdup("Number"),0u,4u,p+32,4u));
        assert(!R("RegSetValueExA",0x80000001u,gstrdup("DS3RootFixture"),0u,4u,p+32,4u));
        assert(!R("RegCloseKey",h));
    } else assert(!rc);
    wr32(p+4,4);
    assert(!R("RegQueryValueExA",0x80000001u,gstrdup("DS3RootFixture"),0u,p+8,p+24,p+4) && rd32(p+24)==42);
    assert(!R("RegOpenKeyExW",0x80000001u,wpath,0u,0x20019u,p));
    h=rd32(p);
    wr32(p+4,1);
    assert(R("RegQueryValueExA",h,lang,0u,p+8,p+24,p+4)==234 && rd32(p+4)==6);
    assert(!R("RegQueryValueExA",h,lang,0u,p+8,p+24,p+4) && rd32(p+8)==1 && !strcmp((char*)M+p+24,"ru_RU"));
    wr32(p+4,32);
    assert(!R("RegQueryValueExW",h,wlang,0u,p+8,p+24,p+4) && rd32(p+4)==12 && rd16(p+24)=='r');
    wr32(p+4,4);
    assert(!R("RegQueryValueExA",h,gstrdup("Number"),0u,p+8,p+24,p+4) && rd32(p+8)==4 && rd32(p+24)==42);
    if (!strcmp(argv[4],"write")) {
        assert(R("RegOpenKeyExA",0x80000001u,path,0u,0x20119u,p+16)==50);
        assert(R("RegSetValueExA",h,gstrdup("Number"),0u,4u,p+24,4u)==5);
        assert(R("RegCreateKeyExW",0x80000001u,hle_wstr("Software\\line\nbreak"),0u,0u,0u,0x2001fu,0u,p+16,0u)==87);
    }
    assert(!R("RegCloseKey",h));
    assert(R("RegQueryValueExA",h,lang,0u,p+8,p+24,p+4)==6);
    assert(!R("RegOpenKeyExA",0x80000002u,gstrdup("Software\\Ageia Technologies"),0u,0x20019u,p));
    h=rd32(p); wr32(p+4,64);
    assert(!R("RegQueryValueExA",h,gstrdup("PhysXCore Path"),0u,p+8,p+24,p+4) && !strcmp((char*)M+p+24,"C:\\MassEffect\\Binaries\\"));
    assert(!R("RegCloseKey",h));
    puts("PASS profile registry A/W, sizes, DWORD and PhysX");
}
