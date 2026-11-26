#include "audio-out.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

static const struct ds_audio_ops* active;
static const struct { const char* name; const struct ds_audio_ops* ops; } backends[]={
    {"alsa",&ds_audio_alsa},{"pipewire",&ds_audio_pipewire},{"pulse",&ds_audio_pulse}
};

int ds_audio_open(const char* mode,const char* device,int rate,const char** name) {
    if (active) return -EBUSY;
    if (name) *name=NULL;
    if (!mode || !*mode) mode="auto";
    int automatic=!strcmp(mode,"auto"), result=-EINVAL;
    for (unsigned i=0;i<sizeof backends/sizeof *backends;++i) {
        const char* backend=backends[i].name;
        if (!automatic && strcmp(mode,backend)) continue;
        const struct ds_audio_ops* ops=backends[i].ops;
        fprintf(stderr,"[ds3-audio-out] attempt=%s\n",backend);
        if (i==0 && (!device || !*device)) {
            fprintf(stderr,"[ds3-audio-out] alsa: set DS3_ALSA_DEVICE=hw:CARD=<card>,DEV=<n>\n");
            continue;
        }
        const char* why=NULL;
        result=ops->open(device,rate,1500,&why);
        if (result>0) {
            active=ops; if (name) *name=backend;
            fprintf(stderr,"[ds3-audio-out] selected=%s rate=%d\n",backend,result);
            return result;
        }
        fprintf(stderr,"[ds3-audio-out] %s: %s (%d)\n",backend,why?why:"cannot open",result);
        ops->close();
    }
    fprintf(stderr,"[ds3-audio-out] no usable output for mode=%s\n",mode);
    return result<0?result:-ENODEV;
}

int ds_audio_start(ds_render_fn render) { return active?active->start(render):-ENODEV; }
void ds_audio_close(void) { if (active) { active->close(); active=NULL; } }
