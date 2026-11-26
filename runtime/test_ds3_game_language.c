#include "hle.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t call(const char* name, unsigned n, uint32_t* args) {
    uint32_t fn=hle_resolve_impl("kernel32.dll",name);
    assert(fn);
    return rt_call(&g_cpu,fn,n,args);
}

int main(int argc, char** argv) {
    assert(argc==2);
    rt_init();
    const char* expected=strcmp(argv[1],"unset") ? argv[1] : NULL;
    for (unsigned wide=0; wide<=1; ++wide) {
        const char* get=wide?"GetEnvironmentStringsW":"GetEnvironmentStrings";
        uint32_t block=call(get,0,NULL), p=block;
        unsigned hit=0, stride=wide?2:1;
        assert(block);
        while (wide?rd16(p):rd8(p)) {
            char entry[1024]; unsigned n=0, ch;
            while ((ch=wide?rd16(p):rd8(p))) {
                assert(n<sizeof entry-1); entry[n++]=(char)ch; p+=stride;
            }
            p+=stride; entry[n]=0;
            assert(strncmp(entry,"DS3_TEST_PRIVATE=",17));
            if (!strncmp(entry,"EAGameLocale=",13)) {
                assert(expected && !strcmp(entry+13,expected)); ++hit;
            }
        }
        assert(hit==!!expected);
        uint32_t out=ghalloc(16,1), args[]={wide?hle_wstr("eagamelocale"):gstrdup("eagamelocale"),out,8};
        assert(call(wide?"GetEnvironmentVariableW":"GetEnvironmentVariableA",3,args)==(expected?5u:0u));
        if (expected) for (unsigned i=0;i<6;++i) assert((wide?rd16(out+2*i):rd8(out+i))==(unsigned char)expected[i]);
        assert(call(wide?"FreeEnvironmentStringsW":"FreeEnvironmentStringsA",1,&block));
    }
    puts("PASS: Android guest locale (A/W)");
}
