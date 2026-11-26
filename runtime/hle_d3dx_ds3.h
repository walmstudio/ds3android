/* D3DX9_43 HLE subset. Uses the same COM/resource lifetime and upload queue as
 * D3D9. No private DLL or native Windows loader. Unsupported transforms fail. */
static int ct_range(Obj *o, uint32_t off, uint32_t n) { return off <= o->size && n <= o->size-off; }
static int ct_string(Obj *o, uint32_t off) {
    return ct_range(o,off,1) && memchr(M+o->shadow+off,0,o->size-off)!=NULL;
}
/* Handles are opaque one-based node indices, never pointers into caller data.
 * Nodes live in Obj.blob so normal deferred COM destruction frees them. */
typedef struct {
    uint32_t name,type,set,index,count,def,elements,bytes,stride;
    uint32_t child,next,element;
} CtNode;
#define CT(o,h) (((CtNode*)(o)->blob)[(h)-1])
static uint32_t ct_entry(Obj *o, uint32_t h) { return o && o->type==T_CTAB && h && h<=o->qtype ? h : 0; }
static uint32_t ct_type_regs(Obj *o,uint32_t t,uint32_t set,int depth,uint32_t *bytes) {
    uint32_t b=o->shadow;
    if (depth>16 || set>3 || !ct_range(o,t,16)) return 0;
    uint32_t cls=rd16(b+t),rows=rd16(b+t+4),cols=rd16(b+t+6),elems=rd16(b+t+8),members=rd16(b+t+10);
    if (!elems || elems>4096) return 0;
    uint64_t regs=0,nbytes=0;
    if (cls==5) {
        uint32_t m=rd32(b+t+12);
        if (!members || members>256 || !ct_range(o,m,members*8)) return 0;
        for (uint32_t i=0;i<members;i++) {
            uint32_t cb=0,r;
            if (!ct_string(o,rd32(b+m+i*8)) || !(r=ct_type_regs(o,rd32(b+m+i*8+4),set,depth+1,&cb))) return 0;
            regs+=r; nbytes+=cb;
        }
    } else {
        if (members || cls>4 || !rows || !cols || rows>4 || cols>4) return 0;
        regs=set==0?rows*cols:cls==2?rows:cls==3?cols:1; nbytes=(uint64_t)rows*cols*4;
    }
    regs*=elems; nbytes*=elems;
    if (regs>65536 || nbytes>0xffffffffu) return 0;
    *bytes=(uint32_t)nbytes; return (uint32_t)regs;
}
static uint32_t ct_node(Obj *o,uint32_t name,uint32_t type,uint32_t set,uint32_t index,
                        uint32_t available,uint32_t def,int single,int depth) {
    uint32_t bytes=0,regs=ct_type_regs(o,type,set,depth,&bytes),b=o->shadow;
    if (!regs || !ct_string(o,name) || o->qtype>=8192) return 0;
    uint32_t elems=rd16(b+type+8),stride=regs/elems;
    if (single) { regs=stride; bytes/=elems; elems=1; }
    uint32_t h=++o->qtype;
    if (h*sizeof(CtNode)>o->bloblen) {
        uint32_t cap=(h+63)&~63u; void *p=realloc(o->blob,cap*sizeof(CtNode));
        if (!p) { o->qtype--; return 0; } o->blob=p; o->bloblen=cap*sizeof(CtNode);
    }
    CtNode n={name,type,set,index,regs<available?regs:available,def,elems,bytes,stride,0,0,0};
    CT(o,h)=n;
    if (rd16(b+type)==5) {
        uint32_t members=rd16(b+type+10),m=rd32(b+type+12),offset=0,last=0;
        for (uint32_t i=0;i<members;i++) {
            uint32_t cb=0,mt=rd32(b+m+i*8+4),mr=ct_type_regs(o,mt,set,depth+1,&cb);
            uint32_t left=offset<n.count?n.count-offset:0;
            uint32_t ch=ct_node(o,rd32(b+m+i*8),mt,set,index+offset,left,def?def+offset*(set==0?4:16):0,0,depth+1);
            if (!ch) return 0;
            if (last) CT(o,last).next=ch; else CT(o,h).child=ch;
            last=ch; offset+=mr;
        }
    }
    return h;
}
static uint32_t ct_unsupported(CPU *c) { (void)c; return ERR_INVALIDCALL; }
static uint32_t ct_desc(CPU *c) {
    Obj *o=SELF; uint32_t p=A(1); if (!o || !p) return ERR_INVALIDCALL;
    uint32_t b=o->shadow, creator=rd32(b+4);
    wr32(p,creator?b+creator:0); wr32(p+4,rd32(b+8)); wr32(p+8,rd32(b+12)); return OK_;
}
static uint32_t ct_constant(CPU *c) {
    Obj *o=SELF; uint32_t parent=A(1);
    if (!o || (parent && !ct_entry(o,parent))) return 0;
    uint32_t h=parent?CT(o,parent).child:o->fvf;
    for (uint32_t i=0;h && i<A(2);i++) h=CT(o,h).next;
    return h;
}
static uint32_t ct_array_element(Obj *o,uint32_t h,uint32_t element) {
    if (!ct_entry(o,h) || element>=CT(o,h).elements) return 0;
    CtNode n=CT(o,h); if (n.elements==1) return h;
    if (!n.element) {
        uint32_t first=0,last=0;
        for (uint32_t i=0;i<n.elements;i++) {
            uint32_t offset=i*n.stride,left=offset<n.count?n.count-offset:0;
            uint32_t ch=ct_node(o,n.name,n.type,n.set,n.index+offset,left,n.def?n.def+offset*(n.set==0?4:16):0,1,0);
            if (!ch) return 0;
            if (last) CT(o,last).next=ch; else first=ch; last=ch;
        }
        CT(o,h).element=first;
    }
    uint32_t e=CT(o,h).element; while (element--) e=CT(o,e).next; return e;
}
static uint32_t ct_name(CPU *c) {
    Obj *o=SELF; uint32_t parent=A(1); if (!o || !A(2) || (parent && !ct_entry(o,parent))) return 0;
    const char *p=GSTR(A(2)); uint32_t list=parent?CT(o,parent).child:o->fvf;
    for (unsigned depth=0;depth<32;depth++) {
        size_t len=strcspn(p,".["); if (!len) return 0;
        uint32_t h=list;
        while (h && (strlen(GSTR(o->shadow+CT(o,h).name))!=len || strncmp(GSTR(o->shadow+CT(o,h).name),p,len))) h=CT(o,h).next;
        if (!h) return 0; p+=len;
        while (*p=='[') {
            char *end; unsigned long ix=strtoul(p+1,&end,10);
            if (end==p+1 || *end!=']' || ix>0xffffffffu || !(h=ct_array_element(o,h,(uint32_t)ix))) return 0;
            p=end+1;
        }
        if (!*p) return h;
        if (*p++!='.') return 0; list=CT(o,h).child;
    }
    return 0;
}
static uint32_t ct_element(CPU *c) { return ct_array_element(SELF,A(1),A(2)); }
static uint32_t ct_sampler(CPU *c) {
    Obj *o=SELF; uint32_t h=ct_entry(o,A(1)); return h && CT(o,h).set==3 ? CT(o,h).index : 0xffffffffu;
}
static uint32_t ct_constant_desc(CPU *c) {
    Obj *o=SELF; uint32_t h=ct_entry(o,A(1)), p=A(2), n=A(3);
    if (!h || !p || !n || !rd32(n)) return ERR_INVALIDCALL;
    CtNode node=CT(o,h); uint32_t b=o->shadow, t=b+node.type, def=node.def;
    wr32(p,b+node.name); wr32(p+4,node.set); wr32(p+8,node.index); wr32(p+12,node.count);
    for (int i=0;i<6;i++) wr32(p+16+4*i,rd16(t+2*i));
    wr32(p+32,node.elements); wr32(p+40,node.bytes); wr32(p+44,def?b+def:0); wr32(n,1);
    return OK_;
}
static uint32_t x_ds3_ctab(CPU *c) {
    uint32_t s=A(0), out=A(1); if (!out) return ERR_INVALIDCALL; wr32(out,0);
    if (!s || (rd32(s)>>16!=0xfffe && rd32(s)>>16!=0xffff)) return ERR_INVALIDCALL;
    uint32_t len=shader_len(s)*4; if (len<8) return ERR_INVALIDCALL;
    for (uint32_t off=4;off+4<=len;) {
        uint32_t tok=rd32(s+off);
        if ((tok&0xffff)==0xfffe) {
            uint32_t bytes=((tok>>16)&0x7fff)*4;
            if (bytes>len-off-4) return ERR_INVALIDCALL;
            if (bytes>=32 && rd32(s+off+4)==FCC('C','T','A','B')) {
                uint32_t b=s+off+8, size=bytes-4, count=rd32(b+12), first=rd32(b+16);
                Obj tmp={0}; tmp.shadow=b; tmp.size=size;
                if (rd32(b)!=28 || count>size/20 || !ct_range(&tmp,first,count*20) ||
                    (rd32(b+4) && !ct_string(&tmp,rd32(b+4)))) return ERR_INVALIDCALL;
                for (uint32_t i=0;i<count;i++) {
                    uint32_t h=b+first+i*20, t=rd32(h+12), d=rd32(h+16);
                    if (!ct_string(&tmp,rd32(h)) || !ct_range(&tmp,t,16) ||
                        (d && !ct_range(&tmp,d,rd16(h+8)*(rd16(h+4)==0?4u:16u)))) return ERR_INVALIDCALL;
                    uint32_t cb=0; if (!ct_type_regs(&tmp,t,rd16(h+4),0,&cb)) return ERR_INVALIDCALL;
                }
                Obj *o=newobj(T_CTAB); o->size=size; o->shadow=galloc(size); memcpy(M+o->shadow,M+b,size);
                uint32_t last=0;
                for (uint32_t i=0;i<count;i++) {
                    uint32_t p=b+first+i*20;
                    uint32_t h=ct_node(o,rd32(p),rd32(p+12),rd16(p+4),rd16(p+6),rd16(p+8),rd32(p+16),0,0);
                    if (!h) { destroy(o); return ERR_INVALIDCALL; }
                    if (last) CT(o,last).next=h; else o->fvf=h; last=h;
                }
                wr32(out,o->g); return OK_;
            }
            off+=bytes+4;
        } else if ((tok&0xffff)==0xffff) break;
        else off+=4+4*((tok>>24)&15);
    }
    return ERR_INVALIDCALL;
}
static uint32_t x_ds3_texture(CPU *c) {
    uint32_t out=A(14), s=A(1), n=A(2);
    if (!out) return ERR_INVALIDCALL; wr32(out,0);
    Obj *dev=O(A(0));
    if (!dev || dev->type!=T_DEV || !s || n<128 || rd32(s)!=FCC('D','D','S',' ') || rd32(s+4)!=124 || rd32(s+76)!=32)
        return ERR_INVALIDCALL;
    uint32_t w=rd32(s+16), h=rd32(s+12), levels=rd32(s+28), flags=rd32(s+80), sf=0;
    if (!levels) levels=1;
    if (!w || !h || w>8192 || h>8192 || rd32(s+112) || rd32(s+24)>1) return ERR_INVALIDCALL;
    if (flags&4) {
        sf=rd32(s+84);
        if (sf!=FCC('D','X','T','1') && sf!=FCC('D','X','T','3') && sf!=FCC('D','X','T','5')) return ERR_INVALIDCALL;
    } else if ((flags&0x40) && rd32(s+88)==32 && rd32(s+92)==0xff0000 && rd32(s+96)==0xff00 && rd32(s+100)==0xff)
        sf=(flags&1) && rd32(s+104)==0xff000000 ? 21 : 22;
    if (!sf) return ERR_INVALIDCALL;
    uint32_t fmt=A(7), lv=A(5), ow=A(3), oh=A(4);
    if (!fmt || fmt==0xffffffffu) fmt=sf;
    if (!lv || lv==0xffffffffu) lv=full_levels(w,h,1);
    if (!ow || ow==0xffffffffu) ow=w; if (!oh || oh==0xffffffffu) oh=h;
    if (ow!=w || oh!=h || !lv || lv>levels || levels>full_levels(w,h,1) || A(11) || A(13) || A(6) || A(8)>3 ||
        !((fmt==sf) || ((fmt==21 || fmt==22) && (sf==21 || sf==22)))) return ERR_INVALIDCALL;
    if ((A(9)!=0xffffffffu && A(9)>5) || (A(10)!=0xffffffffu && A(10)>5) ||
        ((sf==21 || sf==22) && (rd32(s+8)&8) && rd32(s+20)!=w*4)) return ERR_INVALIDCALL;
    /* Only unfiltered copying is implemented. Filters cannot affect same-size
     * identical-format pixels; mip generation and color keys are not accepted. */
    uint64_t need=128;
    for (uint32_t l=0;l<lv;l++) {
        uint32_t lw=w>>l? w>>l:1, lh=h>>l? h>>l:1;
        need+=sf==21 || sf==22 ? (uint64_t)lw*lh*4 : (uint64_t)((lw+3)/4)*((lh+3)/4)*(sf==FCC('D','X','T','1')?8:16);
    }
    if (need>n) return ERR_INVALIDCALL;
    uint32_t hr=create_tex(T_TEX,w,h,1,lv,0,fmt,A(8),out); if (hr) return hr;
    Obj *o=O(rd32(out)); uint32_t src=s+128, lr=galloc(8);
    for (uint32_t l=0;l<lv;l++) {
        uint32_t lw=w>>l? w>>l:1, lh=h>>l? h>>l:1;
        uint32_t pitch=sf==21 || sf==22 ? lw*4 : ((lw+3)/4)*(sf==FCC('D','X','T','1')?8:16);
        uint32_t rows=sf==21 || sf==22 ? lh : (lh+3)/4;
        hr=lock_sub(o,l,l,0,0,lr,0); if (hr) break;
        uint32_t dst=rd32(lr+4), dp=rd32(lr);
        for (uint32_t y=0;y<rows;y++) {
            memcpy(M+dst+y*dp,M+src+y*pitch,pitch);
            if (sf==22 && fmt==21) for (uint32_t x=0;x<lw;x++) M[dst+y*dp+x*4+3]=255;
        }
        src+=pitch*rows; hr=unlock_sub(o,l,l); if (hr) break;
    }
    gfree(lr,8);
    if (hr) { destroy(o); wr32(out,0); return hr; }
    if (A(12)) {
        uint32_t p=A(12); wr32(p,w); wr32(p+4,h); wr32(p+8,1); wr32(p+12,levels);
        wr32(p+16,sf); wr32(p+20,3); wr32(p+24,4); /* TEXTURE, DDS */
    }
    return OK_;
}
