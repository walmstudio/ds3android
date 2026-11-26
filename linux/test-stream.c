/* Run real vsetup/index preparation and GEARMASS with an already mapped ring. */
#include "hle_d3d9.c"
#include <assert.h>

static unsigned dispatches;
int __wrap_cmd_on_consumer(void) { return 1; }
void __real_gm_parallel_for(int,gm_job_fn,void*,int);
void __wrap_gm_parallel_for(int n,gm_job_fn fn,void *ctx,int chunk) {
    ++dispatches;
    __real_gm_parallel_for(n,fn,ctx,chunk);
}
static uint8_t out[2][2<<20];
static Chunk chunks[2];
static uint32_t source(size_t n, unsigned salt) {
    uint32_t a=ghalloc(n,0); assert(a);
    for (size_t i=0;i<n;i++) M[a+i]=(uint8_t)((i*17)^(i>>7)^(salt*23));
    return a;
}
static void same(int slot, size_t offset, uint32_t addr, size_t n) {
    assert(!memcmp(out[slot]+offset,M+addr,n));
    assert(out[slot][offset+n]==0xa5);
}
int main(int argc,char **argv) {
    assert(argc==3 || argc==4);
    int enabled=atoi(argv[1]), workers=atoi(argv[2]);
    int shadow=argc==4 && atoi(argv[3])!=0;
    int attested=shadow && atoi(argv[3])==1;
    setenv("ME1_VK_SHADOW_COPY",shadow?"1":"0",1);
    setenv("ME1_VK_STREAM_COPY",argv[1],1);
    setenv("ME1_GEARMASS","custom",1);
    setenv("ME1_GM_WORKERS",argv[2],1);
    setenv("ME1_GM_LOG","0",1);
    rt_init(); gm_ds3_config(); gm_init(); lock_(); unlock_(); g_vk=1; g_cmd=2;
    VPP.limits.maxVertexInputBindings=16;
    g_nfp=1; g_fp[0].f=VK_FORMAT_R32_SFLOAT;
    g_fp[0].p.bufferFeatures=VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT;
    memset(out,0xa5,sizeof out);
    for (int s=0;s<2;s++) {
        chunks[s]=(Chunk){.p=out[s],.size=sizeof out[s],.buf=(VkBuffer)(uintptr_t)(s+1)};
        S[s].vtx=(Ring){.c=&chunks[s],.n=1,.cap=1};
    }
    uint8_t elems[]={0,0,0,0,0,0,0,0, 1,0,0,0,0,0,10,0, 255};
    Obj decl={.blob=elems}; Dev d={.decl=&decl}; GlSh vs={0},ps={0};
    vs.s.nin=1; vs.s.in[0]=(Sm3Sem){.usage=0};
    DrawCall dc={.up=1,.indexed=1,.minv=1,.nv=32769,.count=131073,.upstride=12,.upifmt=102};
    size_t vlen=32769*12, ilen=131073*4;
    dc.upv=source(vlen+12,1); dc.upi=source(ilen,2);
    VLay l; assert(!vsetup(&d,&vs.s,&dc,&l,0));
    assert(l.nb==1 && l.b[0].off==0 && l.b[0].stride==12 && l.voff==-1);
    same(0,0,dc.upv+12,vlen);
    VDrawPrep p={.d=&d,.dc=&dc,.vs=&vs,.ps=&ps,.layout=&l}; v_draw_data_prepare(&p);
    assert(p.ok && p.it==VK_INDEX_TYPE_UINT32 && p.ibo==393232);
    same(0,p.ibo,dc.upi,ilen);

    g_si=1; vs.s.nin=2; vs.s.in[1]=(Sm3Sem){.reg=1,.usage=10};
    Obj a={.size=500000,.shadow=source(500000,3)},b={.size=500000,.shadow=source(500000,4)},
        ib={.size=300000,.shadow=source(300000,5),.fmt=101};
    d.vb[0]=&a; d.vb[1]=&b; d.ib=&ib;
    d.vbsh[0]=source(a.size,6); d.vbsh[1]=source(b.size,7); d.ibsh=source(ib.size,8);
    d.vbver[0]=1; d.vbver[1]=2; d.ibver=3;
    if (attested) { a.shadow=0; b.shadow=0; ib.shadow=0; } /* captured route, not the live object */
    d.vbstride[0]=20; d.vbstride[1]=28; d.vboff[0]=16; d.vboff[1]=7; d.vbfreq[1]=0x80000001;
    dc=(DrawCall){.indexed=1,.start=3,.count=131073,.base=1,.minv=2,.nv=13108,.ninst=11};
    dc.shadow_safe=attested;
    assert(!vsetup(&d,&vs.s,&dc,&l,0));
    assert(l.nb==2 && l.b[0].buf==(VkBuffer)2 && l.b[1].inst && l.b[1].off==262160);
    assert(!memcmp(out[1],M+d.vbsh[0]+76,262160));
    same(1,l.b[1].off,d.vbsh[1]+7,308);
    p=(VDrawPrep){.d=&d,.dc=&dc,.vs=&vs,.ps=&ps,.layout=&l};
    v_draw_data_prepare(&p);
    assert(p.ok && p.it==VK_INDEX_TYPE_UINT16 && p.ibo==262480);
    same(1,p.ibo,d.ibsh+6,262146);
    if (getenv("TEST_RANGE")) {
        VkDeviceSize first=l.b[0].off;
        assert(!vsetup(&d,&vs.s,&dc,&l,0));
        int should_hit=!strcmp(getenv("TEST_RANGE"),"on");
        if ((l.b[0].off==first)!=should_hit) {
            fputs("unchanged vertex range uploaded again\n",stderr); return 2;
        }
        if (vk_range_mode()) {
            assert(g_range_total.possible==262468);
            assert(g_range_total.reused==(should_hit?262468:0));
            assert(g_range_total.requested==787082);
            assert(g_range_total.uploaded==(should_hit?524614:787082));
            vtx_slot_reset(&S[1]);
            memset(out[1],0xa5,sizeof out[1]);
            assert(!vsetup(&d,&vs.s,&dc,&l,0));
            assert(!memcmp(out[1],M+d.vbsh[0]+76,262160));
            assert(g_range_total.reused==(should_hit?262468:0));
            /* Reset clears lookup metadata but must not reclaim live bytes. */
            VkDeviceSize old_end=chunks[1].off;
            vk_clear_range_cache(NULL);
            assert(chunks[1].off==old_end);
            assert(!vsetup(&d,&vs.s,&dc,&l,0));
            assert(l.b[0].off>=old_end);
            assert(!memcmp(out[1]+l.b[0].off,M+d.vbsh[0]+76,262160));
        }
        puts("PASS repeated range binding");
        return 0;
    }
    unsigned expected=workers ? 2*(!!enabled+attested) : 0;
    if (dispatches!=expected) { fprintf(stderr,"expected %u stream jobs; got %u\n",expected,dispatches); return 1; }
    /* Workers may not wake in time for two copies on a busy machine. Repeat
     * a qualifying index draw on a reset ring, checking every copied byte. */
    if (expected) {
        uint32_t addr=source(ilen,9);
        dc=(DrawCall){.up=!attested,.shadow_safe=attested,.indexed=1,.count=131073,.upifmt=102,.upi=addr};
        ib.fmt=102; ib.size=ilen; d.ibsh=addr;
        for (int i=0;i<64 && !(attested?g_vstream.shadow_worker_jobs:g_vstream.worker_jobs);i++) {
            ring_reset(&S[1].vtx); memset(out[1],0xa5,sizeof out[1]);
            p=(VDrawPrep){.d=&d,.dc=&dc,.vs=&vs,.ps=&ps,.layout=&l}; v_draw_data_prepare(&p);
            assert(p.ok && !p.ibo);
            same(1,0,addr,ilen);
        }
        assert(attested ? g_vstream.shadow_worker_bytes>0 : g_vstream.worker_jobs>0);
    }
    puts("PASS UP, renamed snapshot, stride and instance streams, indices 16/32, tails and ring slots");
}
