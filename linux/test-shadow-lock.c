#include "hle_d3d9.c"
#include <assert.h>
#include <semaphore.h>

static sem_t entered, release_draw, waiting;
static uint32_t vb_addr, ib_addr;
static uint64_t observed_version[2];
static int check_bytes, expected_safe, fail_alloc, notify_wait, fail_retention;
void* __real_realloc(void*,size_t);
void* __wrap_realloc(void* ptr,size_t n) {
    if (fail_retention && ptr==g_zomb) return NULL;
    return __real_realloc(ptr,n);
}
static uint8_t copied[655360+16];
static Chunk chunk={.p=copied,.size=sizeof copied,.buf=(VkBuffer)1};
uint32_t __real_hle_vmem_alloc(uint32_t,uint32_t,uint32_t);
uint32_t __wrap_hle_vmem_alloc(uint32_t addr,uint32_t n,uint32_t flags) {
    if (fail_alloc && n==327680) return 0;
    return __real_hle_vmem_alloc(addr,n,flags);
}
void __real_cmd_wait(uint64_t);
void __wrap_cmd_wait(uint64_t pos) {
    if (notify_wait) { assert(cmd_done()<pos); sem_post(&waiting); }
    __real_cmd_wait(pos);
}
static void* unblock(void* opaque) {
    (void)opaque;
    assert(!sem_wait(&waiting));
    sem_post(&release_draw);
    return NULL;
}
static void exec_fixture(CmdHdr* h) {
    if (h->type!=CM_DRAW) { q_exec(h); return; }
    CDraw* r=(CDraw*)h;
    assert(r->mir->vbsh[0]==vb_addr && r->mir->ibsh==ib_addr);
    assert(r->shadow_safe==expected_safe);
    observed_version[0]=r->mir->vbver[0]; observed_version[1]=r->mir->ibver;
    sem_post(&entered);
    assert(!sem_wait(&release_draw));
    if (check_bytes) {
        VkBuffer buf; VkDeviceSize off;
        ring_reset(&S[0].vtx);
        assert(vstream_upload(M+r->mir->vbsh[0],327680,&buf,&off,VSTREAM_VERTEX,0,0,r->shadow_safe,r->mir->vbver[0]));
        assert(off==0);
        assert(vstream_upload(M+r->mir->ibsh,327680,&buf,&off,VSTREAM_INDEX,0,0,r->shadow_safe,r->mir->ibver));
        assert(off==327680);
        for (size_t i=0;i<327680;i++) assert(copied[i]==0x52 && copied[327680+i]==0x81);
    }
}
static void idle_fixture(int event) { (void)event; }
static uint32_t call(Obj* o,unsigned slot,int n,uint32_t* a) {
    uint32_t fn=rd32(rd32(o->g)+slot*4);
    assert(fn);
    return rt_call(&g_cpu,fn,n,a);
}
static uint32_t lock(Obj* o,uint32_t flags) {
    static uint32_t pp;
    if (!pp) pp=ghalloc(4,1);
    assert(!call(o,11,5,(uint32_t[]){o->g,8,16,pp,flags}));
    return rd32(pp)-8;
}
static void unlock(Obj* o) { assert(!call(o,12,1,(uint32_t[]){o->g})); }
int main(void) {
    setenv("ME1_VK_SHADOW_COPY","1",0);
    rt_init(); lock_(); unlock_();
    uint32_t p=ghalloc(4,1);
    assert(!create_buf(T_VB,327680,0,0,0,p)); Obj* vb=O(rd32(p));
    assert(!create_buf(T_IB,327680,0,101,0,p)); Obj* ib=O(rd32(p));
    vb_addr=lock(vb,0); memset(M+vb_addr,0x52,vb->size); unlock(vb);
    ib_addr=lock(ib,0); memset(M+ib_addr,0x81,ib->size); unlock(ib);
    assert(!sem_init(&entered,0,0) && !sem_init(&release_draw,0,0) && !sem_init(&waiting,0,0));
    uint32_t ring=hle_vmem_alloc(0,1<<20,0x3000u);
    assert(ring && cmd_init(2,M+ring,ring,1<<20,exec_fixture,idle_fixture));
    g_cmd=2; g_vk=1;
    S[0].vtx=(Ring){.c=&chunk,.n=1,.cap=1};
    Dev d={.mir=calloc(1,sizeof(Dev)),.sent=calloc(1,sizeof(Dev)),.alld=1,.ib=ib};
    d.vb[0]=vb; dev_sh(&d);
    const char* reuse=getenv("ME1_VK_RANGE_REUSE");
    int enabled=!strcmp(getenv("ME1_VK_SHADOW_COPY"),"1") || (reuse && strcmp(reuse,"off"));
    expected_safe=enabled;
    q_draw(&d,1,1,163840,1,0,0,16384,0,0,0,0,0,0);
    assert(!sem_wait(&entered));
    uint64_t initial_v=observed_version[0], initial_i=observed_version[1];
    if (vk_range_mode()) assert(initial_v && initial_i && initial_v!=initial_i);
    assert(lock(vb,0x10)==vb_addr); unlock(vb); /* READONLY */
    if (vk_range_mode()) assert(vb->shadow_version==initial_v);
    if (enabled && getenv("FAIL_RETENTION")) {
        pthread_t helper; assert(!pthread_create(&helper,NULL,unblock,NULL));
        fail_retention=notify_wait=1;
        assert(lock(vb,0x1000)==vb_addr);
        fail_retention=notify_wait=0;
        assert(!pthread_join(helper,NULL)); unlock(vb);
        assert(g_shadow_producer.waits==1 && !g_nzomb);
        puts("PASS failed retention waits without publishing a rename");
        return 0;
    }
    uint32_t next_vb=lock(vb,0x1000), next_ib=lock(ib,0x1000);
    if (enabled && (next_vb==vb_addr || next_ib==ib_addr)) {
        fputs("NOOVERWRITE reused a queued shadow\n",stderr); return 1;
    }
    if (!enabled) assert(next_vb==vb_addr && next_ib==ib_addr);
    assert(M[next_vb+31]==0x52 && M[next_ib+31]==0x81);
    M[next_vb+8]=0xa3; M[next_ib+8]=0x29;
    unlock(vb); unlock(ib);
    if (vk_range_mode()) assert(vb->shadow_version>initial_v && ib->shadow_version>initial_i && vb->shadow_version!=ib->shadow_version);
    zomb_drain(); assert(g_nzomb==(enabled?2:0));
    check_bytes=enabled;
    sem_post(&release_draw); cmd_wait(cmd_pos());
    zomb_drain(); assert(!g_nzomb);
    assert(lock(vb,0x1000)==next_vb); unlock(vb);
    assert(lock(ib,0x1000)==next_ib); unlock(ib);
    if (enabled) {
        check_bytes=0; vb_addr=next_vb; ib_addr=next_ib;
        assert(lock(vb,0)==vb_addr && lock(vb,0)==vb_addr); unlock(vb);
        expected_safe=0; dev_sh(&d); q_draw(&d,1,1,3,1,0,0,3,0,0,0,0,0,0);
        assert(!sem_wait(&entered));
        pthread_t helper; assert(!pthread_create(&helper,NULL,unblock,NULL));
        notify_wait=1; assert(lock(vb,0x1000)==vb_addr); notify_wait=0;
        assert(!pthread_join(helper,NULL)); unlock(vb); unlock(vb);

        expected_safe=1; dev_sh(&d); q_draw(&d,1,1,3,1,0,0,3,0,0,0,0,0,0);
        assert(!sem_wait(&entered));
        uint64_t allocated=g_shadow_producer.allocated, preservation=g_shadow_producer.copied;
        assert(lock(vb,0x2000)!=vb_addr); unlock(vb);
        assert(g_shadow_producer.allocated==allocated+327680 && g_shadow_producer.copied==preservation);
        /* Disable the reuse cache for this allocation-failure case only. */
        g_nshc=0; g_shcb=0; fail_alloc=1;
        assert(!pthread_create(&helper,NULL,unblock,NULL));
        notify_wait=1; assert(lock(ib,0x1000)==ib_addr); notify_wait=0; fail_alloc=0;
        assert(!pthread_join(helper,NULL)); unlock(ib);
    }
    puts("PASS queued VB/IB lifetime, copied bytes, READONLY, NOOVERWRITE, nested Lock, DISCARD and allocation failure");
}
