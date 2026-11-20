/* Unit tests for the mouse-look model (src/runtime_mouse.c). No SDL, no game needed.
 *   test_mouse_model          runs the property checks
 *   test_mouse_model dump     prints a scripted run (one line per sample) for the golden comparison */
#include "runtime_mouse.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { R2=0x200, L2=0x100, R3=0x4, L1=0x400, R1=0x800 };
static MouseCfg cfg(void) { return mouse_cfg_default(R2,L2,R3,L1,R1); }
static MouseState fresh(void) { MouseState s={0,0,128,128}; return s; }

static uint32_t rs=12345;
static uint32_t rnd(void) { rs=rs*1664525u+1013904223u; return rs>>8; }

static int dump(void) {
    MouseCfg c=cfg(); MouseState st=fresh();
    for (int variant=0; variant<4; ++variant) {
        if (variant==1) c.resp=0;
        if (variant==2) { c.resp=0; c.axial=1; c.curve=1.4f; c.cap=900; }
        if (variant==3) { c.resp=1; c.cap=1200; c.cap_y=500; c.sens=1.7f; c.decay=0.5f; }
        st=fresh();
        for (int i=0;i<4000;++i) {
            const float dt=0.002f+0.0005f*(float)(rnd()%40);
            int dx=0,dy=0;
            if (rnd()%5==0) { dx=(int)(rnd()%400)-200; dy=(int)(rnd()%240)-120; }
            if (i%700<30) dx+=300;
            mouse_model_step(&c,&st,dt,dx,dy);
            if (i%10==0) printf("%d %d %d\n",variant,st.ox,st.oy);
        }
    }
    return 0;
}

static void test_defaults_and_parsing(void) {
    MouseCfg c=cfg();
    assert(c.look==1 && c.resp==1 && c.btn[0]==R2 && c.btn[1]==L2 && c.btn[2]==R3 && c.btn[3]==L1 && c.btn[4]==R1);
    assert(c.btn[5]==0 && c.btn[6]==0);
    assert(mouse_cfg_set(&c,"mouse_turn_x","2000") && c.turn_x==2000.0f);
    assert(mouse_cfg_set(&c,"mouse_invert_y","1") && c.invert_y==1);
    assert(mouse_cfg_set(&c,"mouse_deadzone_mode","7") && c.axial==1);   /* any non-zero is on */
    assert(mouse_cfg_set(&c,"mouse_deadzone_mode","0") && c.axial==0);
    assert(mouse_cfg_set(&c,"mouse_queue_cap_y","650.5") && c.cap_y==650.5f);
    assert(!mouse_cfg_set(&c,"weapon1","square"));                       /* not a mouse model key */
    assert(!mouse_cfg_set(&c,"mouse_nonsense","1"));
    assert(!mouse_cfg_set(&c,"mouse_look_x","1"));                       /* exact match only */
    const char *keys[]={"mouse_look","mouse_turn_x","mouse_turn_y","mouse_sens","mouse_decay","mouse_deadzone",
        "mouse_queue_cap","mouse_invert_y","mouse_deadzone_mode","mouse_curve","mouse_response","mouse_game_deadzone",
        "mouse_game_sat","mouse_game_split","mouse_speed_low_x","mouse_speed_high_x","mouse_speed_y","mouse_queue_cap_y"};
    for (size_t i=0;i<sizeof(keys)/sizeof(*keys);++i) assert(mouse_cfg_set(&c,keys[i],"1"));
}
static void test_clamp(void) {
    MouseCfg c=cfg(); c.turn_x=1; c.turn_y=-5; c.deadzone=-1;
    mouse_cfg_clamp(&c); assert(c.turn_x==50 && c.turn_y==50 && c.deadzone==0);
    c.deadzone=0.9f; mouse_cfg_clamp(&c); assert(c.deadzone==0.5f);
    c=cfg(); mouse_cfg_clamp(&c); assert(c.turn_x==1500 && c.deadzone==0.28f);   /* defaults untouched */
}
static void test_stick_byte(void) {
    assert(mouse_stick_byte(0)==128 && mouse_stick_byte(1)==255 && mouse_stick_byte(-1)==1);
    assert(mouse_stick_byte(5)==255 && mouse_stick_byte(-5)==0);          /* clipped, not wrapped */
}
static void test_idle_is_centred(void) {
    for (int resp=0; resp<2; ++resp) {
        MouseCfg c=cfg(); c.resp=resp; MouseState st=fresh();
        for (int i=0;i<100;++i) mouse_model_step(&c,&st,0.008f,0,0);
        assert(st.ox==128 && st.oy==128);
    }
}
static void test_flick_directions_and_drain(void) {
    for (int resp=0; resp<2; ++resp) {
        MouseCfg c=cfg(); c.resp=resp; MouseState st=fresh();
        mouse_model_step(&c,&st,0.008f,400,0);
        assert(st.ox>128 && st.oy==128);                                  /* right: x up, y centred */
        st=fresh(); mouse_model_step(&c,&st,0.008f,-400,0); assert(st.ox<128);
        st=fresh(); mouse_model_step(&c,&st,0.008f,0,300);  assert(st.oy>128 && st.ox==128);
        st=fresh(); mouse_model_step(&c,&st,0.008f,0,-300); assert(st.oy<128);
        /* a flick drains: eventually the queue is empty and the stick is back at centre */
        st=fresh(); mouse_model_step(&c,&st,0.008f,500,200);
        int steps=0; while ((st.ox!=128 || st.oy!=128) && steps<5000) { mouse_model_step(&c,&st,0.008f,0,0); ++steps; }
        assert(steps<5000 && fabsf(st.qx)<1 && fabsf(st.qy)<1);
    }
}
static void test_invert_y(void) {
    MouseCfg c=cfg(); MouseState a=fresh(), b=fresh();
    mouse_model_step(&c,&a,0.008f,0,300);
    c.invert_y=1; mouse_model_step(&c,&b,0.008f,0,300);
    assert(a.oy>128 && b.oy<128);
}
static void test_queue_caps(void) {
    MouseCfg c=cfg(); c.cap_y=800; MouseState st=fresh();
    mouse_model_step(&c,&st,0.008f,0,100000);                              /* pitch queue never exceeds its cap */
    assert(st.qy<=800.0f);
    c=cfg(); c.resp=0; c.cap=500; st=fresh();
    mouse_model_step(&c,&st,0.008f,100000,-100000);
    assert(fabsf(st.qx)<=500.0f && fabsf(st.qy)<=500.0f);
}
static void test_sensitivity_scales_input(void) {
    MouseCfg c=cfg(); c.decay=0; c.sens=2.0f; MouseState st=fresh();
    st.qx=0; mouse_model_step(&c,&st,0.0001f,10,0);                       /* dt too small to run the model: only queued */
    assert(fabsf(st.qx-20.0f)<1e-4f && st.ox==128);
}
static void test_small_dt_skips_model(void) {
    MouseCfg c=cfg(); c.resp=0; c.decay=1.0f; MouseState st=fresh();
    mouse_model_step(&c,&st,0.0001f,100,0);                               /* queue 100, no model step */
    mouse_model_step(&c,&st,0.0004f,0,0); assert(st.qx==100.0f);          /* below 0.5 ms: nothing happens */
    c.decay=0; st=fresh(); mouse_model_step(&c,&st,0.0001f,100,0); assert(st.qx==100.0f);
}
static void test_weak_input_dropped(void) {
    MouseCfg c=cfg(); c.resp=1; MouseState st=fresh();
    mouse_model_step(&c,&st,0.008f,0,0); st.qx=0.2f; st.qy=-0.2f;
    mouse_model_step(&c,&st,0.008f,0,0);
    assert(st.ox==128 && st.oy==128 && st.qx==0 && st.qy==0);             /* under 0.25 counts: cleared */
}
int main(int argc, char **argv) {
    if (argc>1 && !strcmp(argv[1],"dump")) return dump();
    test_defaults_and_parsing(); test_clamp(); test_stick_byte(); test_idle_is_centred();
    test_flick_directions_and_drain(); test_invert_y(); test_queue_caps(); test_sensitivity_scales_input();
    test_small_dt_skips_model(); test_weak_input_dropped();
    puts("mouse model ok");
    return 0;
}
