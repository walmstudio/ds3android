/* Source-independent DS3 graphics contracts. Not evidence of a guest menu frame. */
#include "hle.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "gearmass.h"
#include <unistd.h>
static int callbacks;
static void presented(void) { callbacks++; }

static uint32_t call(uint32_t t, int n, ...) {
    uint32_t a[20], sp = g_cpu.esp;
    va_list ap; va_start(ap, n);
    for (int i = 0; i < n; i++) a[i] = va_arg(ap, uint32_t);
    va_end(ap);
    uint32_t r = rt_call(&g_cpu, t, n, a);
    assert(g_cpu.esp == sp);
    return r;
}
static uint32_t method(uint32_t o, unsigned k) { return rd32(rd32(o) + k * 4); }
static uint32_t buf(unsigned n) { uint32_t a = ghalloc(n, 1); memset(M+a, 0, n); return a; }
int main(void) {
    gm_ds3_config();
    rt_init();
    /* Loaded images may span the allocator's legacy ME1 free ranges. The
     * command ring must never overwrite image data (including inline tables). */
    Module image={"graphics-fixture",0x00a00000,0x20000,0,NULL,0,NULL,0,NULL};
    char path[]="/tmp/opencode/ds3-graphics-image-XXXXXX";
    int fd=mkstemp(path); assert(fd>=0);
    void *bytes=calloc(1,image.size); ((uint32_t*)bytes)[0]=0x76543210;
    assert(write(fd,bytes,image.size)==image.size); close(fd); free(bytes);
    rt_load_module(&image,path); unlink(path);
    uint32_t ring=hle_vmem_alloc(0,32u<<20,0x3000);
    assert(ring && ((uint64_t)ring+(32u<<20)<=image.base || ring>=image.base+image.size));
    assert(rd32(image.base)==0x76543210);
    assert(!hle_vmem_alloc(image.base,0x10000,0x3000));
    hle_vmem_free(ring,0,0x8000);
    hle_present_hook=presented;
    uint32_t ct = hle_resolve_impl("d3dx9_43.dll", "D3DXGetShaderConstantTable");
    uint32_t tx = hle_resolve_impl("d3dx9_43.dll", "D3DXCreateTextureFromFileInMemoryEx");
    assert(ct && tx); /* fails on the original runtime */
    uint32_t spi=hle_resolve_impl("user32.dll","SystemParametersInfoA"), policy=buf(24);
    assert(spi); wr32(policy,0x12345678);
    assert(!call(spi,4,10u,0u,policy,0u) && rd32(policy)==0x12345678);
    assert(call(hle_resolve_impl("kernel32.dll","GetLastError"),0)==50);
    uint32_t hook=hle_resolve_impl("user32.dll","SetWindowsHookExA"); assert(hook);
    assert(!call(hook,4,13u,0x12345678u,0u,0u));
    assert(call(hle_resolve_impl("kernel32.dll","GetLastError"),0)==50);
    uint32_t out = buf(64), pp = buf(56);
    uint32_t d3d = call(hle_resolve_impl("d3d9.dll", "Direct3DCreate9"), 1, 32u);
    wr32(pp, 64); wr32(pp+4, 64); wr32(pp+8, 21); wr32(pp+12, 1); wr32(pp+24, 1); wr32(pp+32, 1);
    assert(!call(method(d3d,16),7,d3d,0u,1u,0u,0x40u,pp,out));
    uint32_t dev = rd32(out);
    /* Minimal DDS, two BGRA pixels, no resize/mip generation requested. */
    uint32_t dds = buf(136);
    wr32(dds, 0x20534444); wr32(dds+4,124); wr32(dds+8,0x100f);
    wr32(dds+12,1); wr32(dds+16,2); wr32(dds+20,8); wr32(dds+28,1);
    wr32(dds+76,32); wr32(dds+80,0x41); wr32(dds+88,32);
    wr32(dds+92,0xff0000); wr32(dds+96,0xff00); wr32(dds+100,0xff); wr32(dds+104,0xff000000); wr32(dds+108,0x1000);
    wr32(dds+128,0x124080c0); wr32(dds+132,0x80602010);
    for (uint32_t fmt = 21; fmt <= 22; fmt++) {
        uint32_t info = buf(28);
        assert(!call(tx,15,dev,dds,136u,0xffffffffu,0xffffffffu,1u,0u,fmt,1u,1u,1u,0u,info,0u,out));
        uint32_t tex = rd32(out), lr = buf(8);
        assert(tex && rd32(info)==2 && rd32(info+4)==1);
        assert(!call(method(tex,19),5,tex,0u,lr,0u,0x10u));
        uint32_t p = rd32(lr+4);
        assert((rd32(p)&0xffffff)==0x4080c0 && (rd32(p+4)&0xffffff)==0x602010);
        if (fmt==21) assert(rd32(p)==0x124080c0);
        call(method(tex,20),2,tex,0u); call(method(tex,2),1,tex);
    }
    assert(call(tx,15,dev,dds,136u,2u,1u,1u,0u,0xdeadbeefu,1u,1u,1u,0u,0u,0u,out)==0x8876086cu && !rd32(out));
    /* Flat CTAB: float4 Tint at c3. Offsets are relative to the CTAB header. */
    uint32_t sh = buf(104), b = sh+12;
    wr32(sh,0xffff0300); wr32(sh+4,(23u<<16)|0xfffe); wr32(sh+8,0x42415443);
    wr32(b,28); wr32(b+8,0xffff0300); wr32(b+12,1); wr32(b+16,28);
    wr32(b+28,64); wr16(b+32,2); wr16(b+34,3); wr16(b+36,1); wr32(b+40,48);
    wr16(b+48,1); wr16(b+50,3); wr16(b+52,1); wr16(b+54,4); wr16(b+56,1);
    memcpy(M+b+64,"Tint",5); wr32(sh+100,0xffff);
    assert(!call(ct,2,sh,out)); uint32_t table = rd32(out);
    assert(table);
    uint32_t name = buf(8); memcpy(M+name,"Tint",5);
    uint32_t handle = call(method(table,9),3,table,0u,name);
    assert(handle && !call(method(table,9),3,table,0u,name+1));
    uint32_t desc = buf(56), count = buf(4); wr32(count,1);
    assert(!call(method(table,6),4,table,handle,desc,count));
    assert(rd32(count)==1 && !strcmp((char*)M+rd32(desc),"Tint") && rd32(desc+8)==3 && rd32(desc+12)==1);
    call(method(table,2),1,table);
    /* Observed shader contract: a top-level struct with named members. */
    uint32_t nested=buf(160), nb=nested+12;
    wr32(nested,0xffff0300); wr32(nested+4,(37u<<16)|0xfffe); wr32(nested+8,0x42415443); wr32(nested+156,0xffff);
    wr32(nb,28); wr32(nb+8,0xffff0300); wr32(nb+12,1); wr32(nb+16,28);
    wr32(nb+28,104); wr16(nb+32,2); wr16(nb+34,3); wr16(nb+36,1); wr32(nb+40,48);
    wr16(nb+48,5); wr16(nb+52,1); wr16(nb+54,4); wr16(nb+56,1); wr16(nb+58,1); wr32(nb+60,64);
    wr32(nb+64,112); wr32(nb+68,80);
    wr16(nb+80,1); wr16(nb+82,3); wr16(nb+84,1); wr16(nb+86,4); wr16(nb+88,1);
    memcpy(M+nb+104,"State",6); memcpy(M+nb+112,"Tint",5);
    assert(!call(ct,2,nested,out)); table=rd32(out);
    uint32_t qualified=buf(32); memcpy(M+qualified,"State.Tint",11);
    handle=call(method(table,9),3,table,0u,qualified); assert(handle);
    wr32(count,1); assert(!call(method(table,6),4,table,handle,desc,count));
    assert(rd32(desc+8)==3 && rd32(desc+40)==16 && !strcmp((char*)M+rd32(desc),"Tint"));
    call(method(table,2),1,table);
    /* Flat array descriptor/element lookup retains the register stride. */
    wr16(b+56,2); wr16(b+36,2);
    assert(!call(ct,2,sh,out)); table=rd32(out);
    uint32_t root=call(method(table,9),3,table,0u,name);
    handle=call(method(table,10),3,table,root,1u); assert(handle);
    wr32(count,1); assert(!call(method(table,6),4,table,handle,desc,count));
    assert(rd32(desc+8)==4 && rd32(desc+32)==1 && rd32(desc+40)==16);
    assert(!call(method(table,10),3,table,root,2u)); call(method(table,2),1,table);
    /* Real render-target/depth switch, reset and event query. */
    assert(!call(method(dev,28),8,dev,32u,32u,21u,0u,0u,0u,out,0u)); uint32_t rt=rd32(out);
    assert(!call(method(dev,29),8,dev,32u,32u,75u,0u,0u,0u,out,0u)); uint32_t ds=rd32(out);
    assert(!call(method(dev,37),3,dev,0u,rt)); assert(!call(method(dev,39),2,dev,ds));
    assert(!call(method(dev,38),3,dev,0u,out) && rd32(out)==rt);
    assert(!call(method(dev,40),2,dev,out) && rd32(out)==ds);
    assert(!call(method(dev,118),3,dev,8u,out)); uint32_t q=rd32(out);
    assert(!call(method(q,6),2,q,1u)); assert(!call(method(q,7),4,q,out,4u,1u) && rd32(out));
    assert(!call(method(dev,3),1,dev)); assert(!call(method(dev,16),2,dev,pp));
    assert(!call(method(dev,17),5,dev,0u,0u,0u,0u));
    assert(!hle_d3d9_presented() && !callbacks); /* no real window, no loading callback */
    puts("DS3 focused D3DX/graphics contracts: OK (not a guest frame gate)");
    return 0;
}
