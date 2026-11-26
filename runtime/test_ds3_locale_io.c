#include "hle.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t call(const char* name, unsigned n, uint32_t* a) {
    uint32_t fn=hle_resolve_impl("kernel32.dll",name);
    assert(fn);
    return rt_call(&g_cpu,fn,n,a);
}
#define K(f, ...) call(f,sizeof((uint32_t[]){__VA_ARGS__})/4,(uint32_t[]){__VA_ARGS__})
static uint32_t err(void) { return rd32(g_cpu.fs_base+0x34); }

int main(int argc,char** argv) {
    assert(argc==5);
    rt_init(); hle_fs_layout("C:\\DeadSpace3","C:\\DeadSpace3",1); assert(hle_fs_init(argv[1],argv[2],argv[3]));
    uint32_t p=ghalloc(64,1);
    if (strcmp(argv[4],"io")) {
        uint32_t a=call("GetEnvironmentStrings",0,NULL), w=call("GetEnvironmentStringsW",0,NULL);
        assert(a && w);
        const char* expected=!strcmp(argv[4],"unset")?NULL:argv[4];
        unsigned hit[2]={0,0};
        for (const char* s=(char*)M+a;*s;s+=strlen(s)+1) {
            assert(strncmp(s,"DS3_UNRELATED_ENV=",18));
            if (!strncmp(s,"EAGameLocale=",13)) { assert(expected && !strcmp(s+13,expected)); ++hit[0]; }
        }
        for (uint32_t x=w;rd16(x);x+=2) {
            char s[1024]; unsigned k=0;
            while (rd16(x) && k+1<sizeof s) { s[k++]=(char)rd16(x); x+=2; }
            s[k]=0;
            assert(strncmp(s,"DS3_UNRELATED_ENV=",18));
            if (!strncmp(s,"EAGameLocale=",13)) { assert(expected && !strcmp(s+13,expected)); ++hit[1]; }
        }
        assert(hit[0]==!!expected && hit[1]==!!expected);
        assert(K("FreeEnvironmentStringsA",a) && K("FreeEnvironmentStringsW",w));
        uint32_t key=gstrdup("eagamelocale"), wide=hle_wstr("EAGameLocale");
        if (!strcmp(argv[4],"unset")) {
            assert(!K("GetEnvironmentVariableA",key,p,64u) && err()==203);
            assert(!K("GetEnvironmentVariableW",wide,p,32u) && err()==203);
        } else {
            assert(K("GetEnvironmentVariableA",key,p,64u)==5 && !strcmp((char*)M+p,argv[4]));
            assert(K("GetEnvironmentVariableW",wide,p,32u)==5 && rd16(p)==argv[4][0] && rd16(p+10)==0);
        }
        assert(!K("GetEnvironmentVariableA",gstrdup("DS3_UNRELATED_ENV"),p,64u) && err()==203);
        puts("PASS guest locale isolation"); return 0;
    }
    uint32_t h=K("CreateFileW",hle_wstr("C:\\DeadSpace3\\BIGFILE0.VIV"),0x80000000u,1u,0u,3u,0x40000000u,0u);
    uint32_t ev=K("CreateEventW",0u,1u,0u,0u), ov=ghalloc(20,1);
    assert(h!=UINT32_MAX && ev);
    assert(K("SetFilePointer",h,2u,0u,0u)==2);
    assert(!K("ReadFile",h,p,2u,0u,0u) && err()==87);
    assert(!K("ReadFile",h,p,2u,0u,ov) && err()==50);
    wr32(ov+16,0x12345678u);
    assert(!K("ReadFile",h,p,2u,0u,ov) && err()==6);
    const unsigned offsets[]={4,0,7,20};
    const unsigned lengths[]={2,2,2,2};
    const unsigned expected[]={2,2,1,0};
    const char* want[]={"in","or","l",""};
    for (unsigned i=0;i<4;++i) {
        assert(K("ResetEvent",ev));
        memset(M+ov,0,20); wr32(ov+8,offsets[i]); wr32(ov+16,ev);
        wr32(p+16,999);
        assert(!K("ReadFile",h,p,lengths[i],p+16,ov) && err()==997 && rd32(p+16)==0);
        assert(K("WaitForSingleObject",ev,0u)==0);
        assert(rd32(ov)==(i==3?0xc0000011u:0) && rd32(ov+4)==expected[i]);
        assert(!memcmp(M+p,want[i],expected[i]));
        assert(K("SetFilePointer",h,0u,0u,1u)==2);
    }
    assert(K("CloseHandle",ev) && K("CloseHandle",h));
    puts("PASS Windows overlapped read offset, position, event, short read and EOF");
}
