#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

int main(int argc, char** argv){
    const char* dev = argc>1? argv[1] : "/dev/dri/card0";
    int hold = argc>2? atoi(argv[2]) : 30;
    int fd = open(dev, O_RDWR|O_CLOEXEC);
    if(fd<0){ perror("open"); return 1; }
    ioctl(fd, DRM_IOCTL_SET_MASTER, 0);
    struct drm_mode_card_res res; memset(&res,0,sizeof res);
    if(ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES,&res)){ perror("getres0"); return 1; }
    uint32_t *conns=calloc(res.count_connectors?res.count_connectors:1,4);
    uint32_t *crtcs=calloc(res.count_crtcs?res.count_crtcs:1,4);
    uint32_t *encs =calloc(res.count_encoders?res.count_encoders:1,4);
    res.connector_id_ptr=(uintptr_t)conns; res.crtc_id_ptr=(uintptr_t)crtcs; res.encoder_id_ptr=(uintptr_t)encs;
    res.count_fbs=0; res.fb_id_ptr=0;
    if(ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES,&res)){ perror("getres1"); return 1; }
    printf("card: conns=%u crtcs=%u encs=%u\n", res.count_connectors,res.count_crtcs,res.count_encoders);
    uint32_t colors[]={0x00FF0000,0x0000FF00,0x000000FF,0x00FFFF00};
    int lit=0;
    for(unsigned i=0;i<res.count_connectors;i++){
        struct drm_mode_get_connector c; memset(&c,0,sizeof c); c.connector_id=conns[i];
        if(ioctl(fd,DRM_IOCTL_MODE_GETCONNECTOR,&c)) continue;
        struct drm_mode_modeinfo *modes=calloc(c.count_modes?c.count_modes:1,sizeof(struct drm_mode_modeinfo));
        uint32_t *ce=calloc(c.count_encoders?c.count_encoders:1,4);
        uint32_t *pp=calloc(c.count_props?c.count_props:1,4); uint64_t *pv=calloc(c.count_props?c.count_props:1,8);
        c.modes_ptr=(uintptr_t)modes; c.encoders_ptr=(uintptr_t)ce; c.props_ptr=(uintptr_t)pp; c.prop_values_ptr=(uintptr_t)pv;
        if(ioctl(fd,DRM_IOCTL_MODE_GETCONNECTOR,&c)){ continue; }
        printf("conn %u type=%u conn=%u modes=%u\n",conns[i],c.connector_type,c.connection,c.count_modes);
        if(c.connection!=1||c.count_modes==0) continue;
        struct drm_mode_modeinfo m=modes[0];
        printf("  %ux%u@%u\n",m.hdisplay,m.vdisplay,m.vrefresh);
        uint32_t crtc=0;
        for(unsigned k=0;k<c.count_encoders&&!crtc;k++){ struct drm_mode_get_encoder e; memset(&e,0,sizeof e); e.encoder_id=ce[k];
            if(ioctl(fd,DRM_IOCTL_MODE_GETENCODER,&e)) continue;
            for(unsigned b=0;b<res.count_crtcs;b++) if(e.possible_crtcs&(1u<<b)){ crtc=crtcs[b]; break; } }
        if(!crtc){ printf("  no crtc\n"); continue; }
        struct drm_mode_create_dumb cd; memset(&cd,0,sizeof cd); cd.width=m.hdisplay; cd.height=m.vdisplay; cd.bpp=32;
        if(ioctl(fd,DRM_IOCTL_MODE_CREATE_DUMB,&cd)){ perror("  create_dumb"); continue; }
        struct drm_mode_fb_cmd fb; memset(&fb,0,sizeof fb); fb.width=m.hdisplay; fb.height=m.vdisplay; fb.bpp=32; fb.depth=24; fb.pitch=cd.pitch; fb.handle=cd.handle;
        if(ioctl(fd,DRM_IOCTL_MODE_ADDFB,&fb)){ perror("  addfb"); continue; }
        struct drm_mode_map_dumb md; memset(&md,0,sizeof md); md.handle=cd.handle;
        if(ioctl(fd,DRM_IOCTL_MODE_MAP_DUMB,&md)){ perror("  map"); continue; }
        uint32_t *px=mmap(0,cd.size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,md.offset);
        if(px==MAP_FAILED){ perror("  mmap"); continue; }
        uint32_t col=colors[lit%4];
        for(uint64_t p=0;p<cd.size/4;p++) px[p]=col;
        for(uint32_t y=0;y<m.vdisplay;y++){ uint32_t x=(uint32_t)((uint64_t)y*m.hdisplay/(m.vdisplay?m.vdisplay:1)); if(x<m.hdisplay) px[(uint64_t)y*(cd.pitch/4)+x]=0x00FFFFFF; }
        struct drm_mode_crtc sc; memset(&sc,0,sizeof sc); sc.crtc_id=crtc; sc.fb_id=fb.fb_id; sc.set_connectors_ptr=(uintptr_t)&conns[i]; sc.count_connectors=1; sc.mode=m; sc.mode_valid=1;
        if(ioctl(fd,DRM_IOCTL_MODE_SETCRTC,&sc)){ perror("  setcrtc"); continue; }
        printf("  LIT conn %u crtc %u color %06X\n",conns[i],crtc,col&0xFFFFFF); lit++;
    }
    printf("lit %d connector(s); holding %ds\n",lit,hold); fflush(stdout);
    sleep(hold);
    return 0;
}
