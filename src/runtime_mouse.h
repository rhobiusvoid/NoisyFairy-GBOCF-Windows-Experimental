#ifndef PS4RT_RUNTIME_MOUSE_H
#define PS4RT_RUNTIME_MOUSE_H
/* Mouse-look model and ps4rt_mouse.ini value parsing. No SDL, no runtime state: everything works on
 * a MouseCfg and a MouseState passed in, so it can be unit-tested anywhere (tests/test_mouse_model.c). */
#include <stdint.h>

typedef struct {
    int look; float turn_x, turn_y, sens, decay, deadzone, cap; int invert_y; int axial; float curve;
    int resp; float r_dz, r_sat, r_split, r_low_x, r_high_x, r_y, cap_y;
    uint32_t btn[7]; /* left right middle x1 x2 wheel_up wheel_down (PS4 button masks) */
} MouseCfg;
typedef struct {
    float qx, qy;     /* queued mouse counts per axis */
    uint8_t ox, oy;   /* last stick output (128 = centre) */
} MouseState;

/* Defaults; the button map needs the pad's button masks, so the caller passes them. */
MouseCfg mouse_cfg_default(uint32_t left, uint32_t right, uint32_t middle, uint32_t x1, uint32_t x2);
/* Applies one numeric/boolean ini setting (mouse_look, mouse_turn_x ...). Returns 1 if the key was one of them. */
int mouse_cfg_set(MouseCfg *c, const char *key, const char *value);
/* Keeps values in their safe ranges after a load. */
void mouse_cfg_clamp(MouseCfg *c);
uint8_t mouse_stick_byte(float s); /* -1..1 -> 0..255 around 128 */
/* One poll: adds the counts (dx,dy), applies sensitivity, invert and decay, then updates st->ox/oy and
 * drains the queue. dt in seconds (caller clamps it). */
void mouse_model_step(const MouseCfg *c, MouseState *st, float dt, int dx, int dy);
#endif
