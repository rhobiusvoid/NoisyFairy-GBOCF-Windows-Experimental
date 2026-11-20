/* Mouse-look model; see runtime_mouse.h. The math was moved here unchanged from runtime_pad.c. */
#include "runtime_mouse.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

MouseCfg mouse_cfg_default(uint32_t left, uint32_t right, uint32_t middle, uint32_t x1, uint32_t x2) {
    MouseCfg c={1,1500,1500,1.0f,1.0f,0.28f,0,0,1,1.0f,1,0.300f,0.585f,0.875f,820,1650,720,800,{0,0,0,0,0,0,0}};
    c.btn[0]=left; c.btn[1]=right; c.btn[2]=middle; c.btn[3]=x1; c.btn[4]=x2;
    return c;
}

typedef enum { T_INT, T_FLOAT, T_BOOL } SettingType;
typedef struct { const char *key; SettingType type; size_t offset; } Setting;
#define S(name,type,field) {name,type,offsetof(MouseCfg,field)}
static const Setting settings[]={
    S("mouse_look",T_INT,look), S("mouse_turn_x",T_FLOAT,turn_x), S("mouse_turn_y",T_FLOAT,turn_y),
    S("mouse_sens",T_FLOAT,sens), S("mouse_decay",T_FLOAT,decay), S("mouse_deadzone",T_FLOAT,deadzone),
    S("mouse_queue_cap",T_FLOAT,cap), S("mouse_invert_y",T_INT,invert_y), S("mouse_deadzone_mode",T_BOOL,axial),
    S("mouse_curve",T_FLOAT,curve), S("mouse_response",T_INT,resp), S("mouse_game_deadzone",T_FLOAT,r_dz),
    S("mouse_game_sat",T_FLOAT,r_sat), S("mouse_game_split",T_FLOAT,r_split), S("mouse_speed_low_x",T_FLOAT,r_low_x),
    S("mouse_speed_high_x",T_FLOAT,r_high_x), S("mouse_speed_y",T_FLOAT,r_y), S("mouse_queue_cap_y",T_FLOAT,cap_y),
};
#undef S

int mouse_cfg_set(MouseCfg *c, const char *key, const char *value) {
    for (size_t i=0;i<sizeof(settings)/sizeof(*settings);++i) {
        if (strcmp(key,settings[i].key)) continue;
        void *field=(char*)c+settings[i].offset;
        switch (settings[i].type) {
            case T_INT: *(int*)field=atoi(value); break;
            case T_BOOL: *(int*)field=atoi(value)!=0; break;
            case T_FLOAT: *(float*)field=(float)atof(value); break;
        }
        return 1;
    }
    return 0;
}

void mouse_cfg_clamp(MouseCfg *c) {
    if (c->turn_x<50) c->turn_x=50;
    if (c->turn_y<50) c->turn_y=50;
    if (c->deadzone<0) c->deadzone=0;
    if (c->deadzone>0.5f) c->deadzone=0.5f;
}

/* Both axes together: the turn needed to drain the queue within this poll is a vector (in units of
 * the per-axis speed cap); if it is longer than full stick it is scaled down, not clipped per
 * axis, so a diagonal flick keeps its direction. mouse_curve undoes an expo-style stick response
 * (the game turns ~ stick^curve). mouse_deadzone_mode 1 (axial) lifts each axis past a per-axis
 * game dead zone, 0 (radial) lifts the stick magnitude past a circular one. The output is
 * quantized to the pad's 8-bit stick, and the queue is drained by what the bytes really ask for
 * (error feedback), so rounding steps average out instead of forming a grid. */
uint8_t mouse_stick_byte(float s) { /* s in -1..1 */
    int v=(int)lrintf(128.0f+s*127.0f); return (uint8_t)(v<0 ? 0 : v>255 ? 255 : v);
}
static float mouse_expo(float u, float inv) { return u<=0 ? 0 : powf(u,inv); }
static void model_stick(const MouseCfg *c, MouseState *st, float dt) {
    if (c->cap>0) {
        if (st->qx>c->cap) st->qx=c->cap;
        if (st->qx<-c->cap) st->qx=-c->cap;
        if (st->qy>c->cap) st->qy=c->cap;
        if (st->qy<-c->cap) st->qy=-c->cap;
    }
    float ux=st->qx/(dt*c->turn_x), uy=st->qy/(dt*c->turn_y);
    float mag=sqrtf(ux*ux+uy*uy);
    const float tmin=c->turn_x<c->turn_y ? c->turn_x : c->turn_y;
    if (mag<0.25f/(dt*tmin)) { st->qx=st->qy=0; st->ox=st->oy=128; return; }
    if (mag>1.0f) { ux/=mag; uy/=mag; mag=1.0f; }
    const float dz=c->deadzone, curve=c->curve>0.1f ? c->curve : 1.0f, inv=1.0f/curve;
    float sx, sy;
    if (c->axial) {
        sx=fabsf(st->qx)<0.25f ? 0 : dz+(1.0f-dz)*mouse_expo(fabsf(ux),inv);
        sy=fabsf(st->qy)<0.25f ? 0 : dz+(1.0f-dz)*mouse_expo(fabsf(uy),inv);
        if (ux<0) sx=-sx;
        if (uy<0) sy=-sy;
    } else {
        const float s=dz+(1.0f-dz)*mouse_expo(mag,inv);
        sx=ux/mag*s; sy=uy/mag*s;
    }
    st->ox=mouse_stick_byte(sx); st->oy=mouse_stick_byte(sy);
    const float qx=((float)st->ox-128.0f)/127.0f, qy=((float)st->oy-128.0f)/127.0f;
    if (c->axial) {
        const float ax=fabsf(qx), ay=fabsf(qy);
        if (ax>dz) st->qx-=(qx<0 ? -1 : 1)*powf((ax-dz)/(1.0f-dz),curve)*c->turn_x*dt;
        if (ay>dz) st->qy-=(qy<0 ? -1 : 1)*powf((ay-dz)/(1.0f-dz),curve)*c->turn_y*dt;
    } else {
        const float qm=sqrtf(qx*qx+qy*qy);
        if (qm>dz) {
            const float f=powf((qm-dz)/(1.0f-dz),curve)/qm;
            st->qx-=qx*f*c->turn_x*dt; st->qy-=qy*f*c->turn_y*dt;
        }
    }
}

static float resp_speed(float s, float low, float high, float dz, float sat, float split) {
    s=fabsf(s);
    if (s<=dz) return 0;
    if (high>0 && s>=split) return high;
    const float f=(s-dz)/(sat-dz);
    return low*(f>1 ? 1 : f);
}
static float resp_stick(float v, float low, float high, float dz, float sat) {
    /* v: desired speed (>=0) -> stick magnitude 0..1 */
    if (v<=0) return 0;
    if (v>=low) return (high>0 && v>=(low+high)*0.5f) ? 1.0f : sat+0.03f;
    return dz+0.004f+(sat-dz-0.004f)*(v/low);
}
static void model_stick_resp(const MouseCfg *c, MouseState *st, float dt) {
    const float dz=c->r_dz, sat=c->r_sat, split=c->r_split;
    if (c->cap>0) { if (st->qx>c->cap) st->qx=c->cap;
        if (st->qx<-c->cap) st->qx=-c->cap; }
    const float cy=c->cap_y>0 ? c->cap_y : c->cap;
    if (cy>0) { if (st->qy>cy) st->qy=cy; if (st->qy<-cy) st->qy=-cy; }
    float *q[2]={&st->qx,&st->qy};
    const float low[2]={c->r_low_x,c->r_y}, high[2]={c->r_high_x,0};
    uint8_t *out[2]={&st->ox,&st->oy};
    for (int a=0;a<2;++a) {
        float v=fabsf(*q[a])/dt;
        if (fabsf(*q[a])<0.25f) { *q[a]=0; *out[a]=128; continue; }
        const float sgn=*q[a]<0 ? -1.0f : 1.0f;
        *out[a]=mouse_stick_byte(sgn*resp_stick(v,low[a],high[a],dz,sat));
        const float sb=((float)*out[a]-128.0f)/127.0f;
        *q[a]-=(sb<0 ? -1.0f : 1.0f)*resp_speed(sb,low[a],high[a],dz,sat,split)*dt;
    }
}

void mouse_model_step(const MouseCfg *c, MouseState *st, float dt, int dx, int dy) {
    st->qx+=(float)dx*c->sens;
    st->qy+=(float)(c->invert_y ? -dy : dy)*c->sens;
    if (dt>=0.0005f) {
        if (c->decay>0.01f) { const float f=expf(-dt/c->decay); st->qx*=f; st->qy*=f; }
        if (c->resp) model_stick_resp(c,st,dt); else model_stick(c,st,dt);
    }
}
