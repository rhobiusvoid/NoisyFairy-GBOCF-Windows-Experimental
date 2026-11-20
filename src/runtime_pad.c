/* libScePad on SDL3 gamepads, with a keyboard fallback. SDL events are pumped
 * by the window thread (gpu/shim/window.cpp); here state is only sampled.
 *
 * Default keyboard layout (every key is rebindable: ps4rt_mouse.ini key_* lines, the launcher's
 * Controls tab or the Insert menu; PS4RT_KEY_MAP also changes the buttons, see apply_button_map):
 *   WASD left stick, arrow keys right stick, Space Cross, LShift Circle,
 *   E Square, Q Triangle, Left Ctrl L1, G R1, R L2, F R2, Z L3, C R3,
 *   1-8 weapon slots (the switch button plus the slot's face-button combination),
 *   Enter Options, Tab left touchpad (photo mode), Backspace right touchpad (pause menu),
 *   IJKL d-pad (I up, K down, J left, L right). */
#define _GNU_SOURCE
#include "runtime.h"
#include "gpu/ps4rtgpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <SDL3/SDL.h>
#include <sys/stat.h>
#include "runtime_mouse.h"

#define ERR_INVALID_ARG ((int32_t)0x80920001)
#define ERR_INVALID_HANDLE ((int32_t)0x80920003)
#define ERR_ALREADY_OPENED ((int32_t)0x80920004)
#define ERR_NOT_INITIALIZED ((int32_t)0x80920005)
#define PAD_HANDLE 1

enum {
    BTN_L3=0x2, BTN_R3=0x4, BTN_OPTIONS=0x8, BTN_UP=0x10, BTN_RIGHT=0x20, BTN_DOWN=0x40, BTN_LEFT=0x80,
    BTN_L2=0x100, BTN_R2=0x200, BTN_L1=0x400, BTN_R1=0x800, BTN_TRIANGLE=0x1000, BTN_CIRCLE=0x2000,
    BTN_CROSS=0x4000, BTN_SQUARE=0x8000, BTN_TOUCHPAD=0x100000,
    /* Host-only sources, never shown to the game: a click on one side of the touchpad from a
     * button or key without a touch surface (play_click turns them into a touch and a click). */
    CLICK_LEFT=0x40000000, CLICK_RIGHT=0x80000000,
};
typedef struct { uint16_t x, y; uint8_t id, reserve[3]; } PadTouch;
typedef struct {
    uint32_t buttons;
    uint8_t left_x, left_y, right_x, right_y;
    uint8_t l2, r2, analog_padding[2];
    float orientation[4], acceleration[3], angular_velocity[3];
    uint8_t touch_count, touch_reserve[3];
    uint32_t touch_held_time;
    PadTouch touches[2];
    uint8_t connected, pad0[3];
    uint64_t timestamp;
    uint8_t extension[16];
    uint8_t connected_count, reserve[2], unique_length, unique[12];
} PadData;
typedef struct {
    float pixel_density; uint16_t resolution_x, resolution_y;
    uint8_t dead_zone_left, dead_zone_right, connection_type, connected_count;
    uint8_t connected, pad[3];
    int32_t device_class;
    uint8_t reserve[8];
} ControllerInfo;
_Static_assert(sizeof(PadData)==120,"OrbisPadData layout");
_Static_assert(sizeof(PadTouch)==8,"OrbisPadTouch layout");
_Static_assert(__builtin_offsetof(PadData,touches)==60,"OrbisPadData touch offset");
_Static_assert(__builtin_offsetof(PadData,timestamp)==80,"OrbisPadData timestamp offset");
_Static_assert(sizeof(ControllerInfo)==28,"OrbisPadControllerInformation layout");

static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static int initialized, opened, sdl_ready;
static SDL_Gamepad *gamepad;
static size_t reads;
static uint8_t connected_count;
static uint64_t next_gamepad_scan;

static uint64_t now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (uint64_t)t.tv_sec*1000000u+(uint64_t)t.tv_nsec/1000u; }
static uint8_t axis(int16_t v) { int x=(v+32768)>>8; return (uint8_t)(x<0 ? 0 : x>255 ? 255 : x); }
static uint8_t trigger(int16_t v) { int x=v>>7; return (uint8_t)(x<0 ? 0 : x>255 ? 255 : x); }
static uint16_t touch_axis(float v, int max) {
    return (uint16_t)(v<=0.0f ? 0 : v>=1.0f ? max : (int)(v*max+0.5f));
}

/* A click without a touch surface (Back/View, Tab, Backspace, the pad file) is played like a
 * finger: the touch is reported CLICK_LEAD_US before the button, the button stays at least
 * CLICK_HOLD_US, and the touch lingers CLICK_TAIL_US after release. This keeps
 * click transitions visible to games that sample touch and button state separately.
 * The side is selected at press time and retained through release; L1 can map the
 * right-side source to a left click on controllers without a touchpad. */
enum { CLICK_LEAD_US=50000, CLICK_HOLD_US=50000, CLICK_TAIL_US=100000 };
static struct { int side; uint64_t pressed, released; } click={-1,0,0};
static void play_click(PadData *d) {
    const uint64_t now=d->timestamp;
    uint32_t sources=d->buttons & (CLICK_LEFT|CLICK_RIGHT);
    d->buttons&=~(CLICK_LEFT|CLICK_RIGHT);
    if ((d->buttons & BTN_TOUCHPAD) && !d->touch_count) { /* a replay, or a pad without fingers */
        d->buttons&=~BTN_TOUCHPAD;
        sources|=CLICK_LEFT;
    }
    const int side=(sources & CLICK_LEFT) ? 0 : (sources & CLICK_RIGHT) ? !(d->buttons & BTN_L1) : -1;
    if (side>=0 && (click.side<0 || click.released)) { click.side=side; click.pressed=now; click.released=0; }
    else if (side<0 && click.side>=0 && !click.released) click.released=now;
    if (click.side<0) return;
    const uint64_t down=click.pressed+CLICK_LEAD_US;
    uint64_t up=down+CLICK_HOLD_US;
    if (!click.released) up=UINT64_MAX;
    else if (click.released>up) up=click.released;
    if (now>=up && now-up>=CLICK_TAIL_US) { click.side=-1; return; }
    if (d->touch_count<2) {
        d->touches[d->touch_count]=(PadTouch){.x=click.side ? 1440 : 480,.y=471,.id=d->touch_count};
        ++d->touch_count;
    }
    if (now>=down && now<up) d->buttons|=BTN_TOUCHPAD;
}

static bool gamepad_active(SDL_Gamepad *pad) {
    for (int button=0;button<SDL_GAMEPAD_BUTTON_COUNT;++button)
        if (SDL_GetGamepadButton(pad,(SDL_GamepadButton)button)) return true;
    for (int stick=SDL_GAMEPAD_AXIS_LEFTX;stick<=SDL_GAMEPAD_AXIS_RIGHTY;++stick) {
        const int value=SDL_GetGamepadAxis(pad,(SDL_GamepadAxis)stick);
        if (value>8000 || value<-8000) return true;
    }
    return SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFT_TRIGGER)>4000 ||
           SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)>4000;
}

/* Keep the current pad while in use, but don't let an idle virtual controller hide a
 * physical pad connected later. Other devices are checked only five times per second. */
static SDL_Gamepad *current_gamepad(void) {
    if (!sdl_ready) sdl_ready = SDL_WasInit(SDL_INIT_GAMEPAD) ? 1 : SDL_InitSubSystem(SDL_INIT_GAMEPAD) ? 1 : -1;
    if (sdl_ready<0) return NULL;
    if (gamepad && !SDL_GamepadConnected(gamepad)) { SDL_CloseGamepad(gamepad); gamepad=NULL; }
    if (!gamepad) {
        int count=0;
        SDL_JoystickID *ids=SDL_GetGamepads(&count);
        if (ids && count>0) {
            gamepad=SDL_OpenGamepad(ids[0]);
            if (gamepad) { ++connected_count; printf("Runtime: gamepad connected: %s\n",SDL_GetGamepadName(gamepad)); }
        }
        SDL_free(ids);
    }
    const uint64_t now=now_us();
    if (gamepad && now>=next_gamepad_scan) {
        next_gamepad_scan=now+200000;
        if (!gamepad_active(gamepad)) {
            int count=0;
            SDL_JoystickID *ids=SDL_GetGamepads(&count);
            const SDL_JoystickID selected=SDL_GetGamepadID(gamepad);
            for (int i=0;ids && i<count;++i) {
                if (ids[i]==selected) continue;
                SDL_Gamepad *candidate=SDL_OpenGamepad(ids[i]);
                if (!candidate) continue;
                if (gamepad_active(candidate)) {
                    SDL_RumbleGamepad(gamepad,0,0,0);
                    SDL_CloseGamepad(gamepad);
                    gamepad=candidate;
                    ++connected_count;
                    printf("Runtime: active gamepad selected: %s (id=%u)\n",
                           SDL_GetGamepadName(gamepad),ids[i]);
                    break;
                }
                SDL_CloseGamepad(candidate);
            }
            SDL_free(ids);
        }
    }
    return gamepad;
}
/* Button sources: a host button or key and the PS4 button it presses. The defaults below can be
 * changed per PS4 button with PS4RT_PAD_MAP (gamepad) and PS4RT_KEY_MAP (keyboard), e.g.
 * PS4RT_KEY_MAP="cross=Q,square=Left Shift" or PS4RT_PAD_MAP="cross=b,circle=a": comma-separated
 * <ps4 button>=<SDL key or gamepad button name>. A mapped PS4 button loses its default sources.
 * touchpad_left (alias touchpad) and touchpad_right are the two sides of the touchpad click:
 * photo mode and the pause menu. Back/View (Share on a DualSense) is the right click, and with
 * L1 held the left one. Sticks, triggers, the real touchpad click and the overlay shortcuts are
 * not remapped. */
static const struct { const char *name; uint32_t ps; } ps_names[]={
    {"cross",BTN_CROSS}, {"circle",BTN_CIRCLE}, {"square",BTN_SQUARE}, {"triangle",BTN_TRIANGLE},
    {"l1",BTN_L1}, {"r1",BTN_R1}, {"l2",BTN_L2}, {"r2",BTN_R2}, {"l3",BTN_L3}, {"r3",BTN_R3},
    {"options",BTN_OPTIONS}, {"touchpad",CLICK_LEFT}, {"touchpad_left",CLICK_LEFT}, {"touchpad_right",CLICK_RIGHT},
    {"up",BTN_UP}, {"down",BTN_DOWN}, {"left",BTN_LEFT}, {"right",BTN_RIGHT},
};
typedef struct { int source; uint32_t ps; } ButtonSource;
typedef struct { ButtonSource entries[48]; size_t count; } ButtonMap;
static ButtonMap pad_map={{
    {SDL_GAMEPAD_BUTTON_SOUTH,BTN_CROSS}, {SDL_GAMEPAD_BUTTON_EAST,BTN_CIRCLE},
    {SDL_GAMEPAD_BUTTON_WEST,BTN_SQUARE}, {SDL_GAMEPAD_BUTTON_NORTH,BTN_TRIANGLE},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,BTN_L1}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,BTN_R1},
    {SDL_GAMEPAD_BUTTON_LEFT_STICK,BTN_L3}, {SDL_GAMEPAD_BUTTON_RIGHT_STICK,BTN_R3},
    {SDL_GAMEPAD_BUTTON_START,BTN_OPTIONS}, {SDL_GAMEPAD_BUTTON_BACK,CLICK_RIGHT},
    {SDL_GAMEPAD_BUTTON_TOUCHPAD,BTN_TOUCHPAD},
    {SDL_GAMEPAD_BUTTON_DPAD_UP,BTN_UP}, {SDL_GAMEPAD_BUTTON_DPAD_DOWN,BTN_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT,BTN_LEFT}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT,BTN_RIGHT},
},15};
static ButtonMap key_map={{
    {SDL_SCANCODE_SPACE,BTN_CROSS}, {SDL_SCANCODE_LSHIFT,BTN_CIRCLE}, {SDL_SCANCODE_E,BTN_SQUARE},
    {SDL_SCANCODE_Q,BTN_TRIANGLE}, {SDL_SCANCODE_LCTRL,BTN_L1}, {SDL_SCANCODE_G,BTN_R1},
    {SDL_SCANCODE_R,BTN_L2}, {SDL_SCANCODE_F,BTN_R2}, {SDL_SCANCODE_Z,BTN_L3}, {SDL_SCANCODE_C,BTN_R3},
    {SDL_SCANCODE_RETURN,BTN_OPTIONS}, {SDL_SCANCODE_TAB,CLICK_LEFT}, {SDL_SCANCODE_BACKSPACE,CLICK_RIGHT},
    {SDL_SCANCODE_I,BTN_UP}, {SDL_SCANCODE_K,BTN_DOWN}, {SDL_SCANCODE_J,BTN_LEFT}, {SDL_SCANCODE_L,BTN_RIGHT},
},17};

/* Keys for the sticks: left x-,x+, left y-,y+, right x-,x+, right y-,y+ (0 = unbound). Set from ps4rt_mouse.ini
 * (key_move_* / key_look_*). */
static int stick_keys[8]={SDL_SCANCODE_A,SDL_SCANCODE_D,SDL_SCANCODE_W,SDL_SCANCODE_S,
                          SDL_SCANCODE_LEFT,SDL_SCANCODE_RIGHT,SDL_SCANCODE_UP,SDL_SCANCODE_DOWN};
static const char *stick_key_names[8]={"key_move_left","key_move_right","key_move_up","key_move_down",
                                       "key_look_left","key_look_right","key_look_up","key_look_down"};
/* ini key -> PS4 button name for the keyboard buttons (key_cross=Space ...). */
static const struct { const char *key, *ps; } key_actions[]={
    {"key_cross","cross"},{"key_circle","circle"},{"key_square","square"},{"key_triangle","triangle"},
    {"key_l1","l1"},{"key_r1","r1"},{"key_l2","l2"},{"key_r2","r2"},{"key_l3","l3"},{"key_r3","r3"},
    {"key_options","options"},{"key_touchpad_left","touchpad_left"},{"key_touchpad_right","touchpad_right"},
    {"key_dpad_up","up"},{"key_dpad_down","down"},{"key_dpad_left","left"},{"key_dpad_right","right"},
};
#define KEY_ACTIONS ((int)(sizeof(key_actions)/sizeof(*key_actions)))

/* Weapon slots: one key presses a combination of pad buttons (the game selects a weapon with the
 * switch button plus a face button or a pair of them). The switch button is added to every slot; the
 * combos are editable because players rebind the pad in the game. A slot stays pressed for at least
 * weapon_hold_ms so a quick tap still lasts a few frames; weapon_stagger_ms holds the switch button
 * alone for that long before the others join. */
typedef struct { int key; uint32_t buttons; int was; uint64_t down_at, until; } WeaponSlot;
static WeaponSlot wslot[8]={   /* the game's own shortcut layout: 1 Square, 2 Cross, 3 Circle, 4 Triangle, 5 Square+Triangle, ... */
    {SDL_SCANCODE_1,BTN_SQUARE},{SDL_SCANCODE_2,BTN_CROSS},{SDL_SCANCODE_3,BTN_CIRCLE},
    {SDL_SCANCODE_4,BTN_TRIANGLE},{SDL_SCANCODE_5,BTN_SQUARE|BTN_TRIANGLE},{SDL_SCANCODE_6,BTN_SQUARE|BTN_CROSS},
    {SDL_SCANCODE_7,BTN_CIRCLE|BTN_TRIANGLE},{SDL_SCANCODE_8,BTN_CIRCLE|BTN_CROSS}};
static uint32_t wswitch=BTN_R1;
static int whold_ms=80, wstagger_ms=0;
static void weapon_apply(PadData *d, const bool *k) {
    const uint64_t now=now_us();
    for (int i=0;i<8;++i) {
        WeaponSlot *w=&wslot[i];
        const int down=w->key>0 && k[w->key];
        if (down && !w->was) { w->down_at=now; w->until=now+(uint64_t)whold_ms*1000; }
        w->was=down;
        if (!down && now>=w->until) continue;
        d->buttons|=wswitch;
        if (now-w->down_at>=(uint64_t)wstagger_ms*1000) d->buttons|=w->buttons;
    }
}

static char *trim(char *text) {
    while (*text==' ' || *text=='\t') ++text;
    char *end=text+strlen(text);
    while (end>text && (end[-1]==' ' || end[-1]=='\t' || end[-1]=='\r' || end[-1]=='\n')) *--end=0;
    return text;
}

/* Applies a PS4RT_PAD_MAP/PS4RT_KEY_MAP value; returns how many entries were used. Bad entries are
 * logged and skipped, so their PS4 button keeps its default. */
static int apply_button_map(ButtonMap *map, const char *spec, bool keyboard) {
    if (!spec || !*spec) return 0;
    char copy[1024];
    snprintf(copy,sizeof(copy),"%s",spec);
    int used=0;
    char *entry=copy;
    while (entry) {
        char *next=strchr(entry,',');
        if (next) *next++=0;
        char *eq=strchr(entry,'=');
        if (!eq) {
            if (*trim(entry)) printf("Runtime: pad map: ignored '%s' (expected button=name)\n",trim(entry));
            entry=next;
            continue;
        }
        *eq=0;
        const char *button=trim(entry), *name=trim(eq+1);
        entry=next;
        uint32_t ps=0;
        for (size_t i=0;i<sizeof(ps_names)/sizeof(*ps_names);++i) if (!SDL_strcasecmp(button,ps_names[i].name)) ps=ps_names[i].ps;
        const int source=!*name ? -1 : keyboard ? (int)SDL_GetScancodeFromName(name) : (int)SDL_GetGamepadButtonFromString(name);
        if (!ps || source<=(keyboard ? (int)SDL_SCANCODE_UNKNOWN : (int)SDL_GAMEPAD_BUTTON_INVALID)) {
            printf("Runtime: pad map: ignored '%s=%s'\n",button,name);
            continue;
        }
        size_t kept=0;
        for (size_t i=0;i<map->count;++i) if (map->entries[i].ps!=ps) map->entries[kept++]=map->entries[i];
        map->count=kept;
        if (map->count<sizeof(map->entries)/sizeof(*map->entries)) map->entries[map->count++]=(ButtonSource){source,ps};
        ++used;
    }
    return used;
}

static void load_button_maps(void) {
    static bool loaded;
    if (loaded) return;
    loaded=true;
    const int pad=apply_button_map(&pad_map,getenv("PS4RT_PAD_MAP"),false);
    const int keys=apply_button_map(&key_map,getenv("PS4RT_KEY_MAP"),true);
    if (pad || keys) printf("Runtime: pad map: %d gamepad and %d keyboard buttons remapped\n",pad,keys);
}


/* ---- Mouse look -------------------------------------------------------------------------------
 * ps4rt_mouse.ini (next to ps4rt.ini, created with defaults on first use, re-read when edited):
 *   mouse_look=1        0 = mouse not used. F8 releases/captures the cursor while playing.
 *   mouse_turn_x=1500   mouse counts per second the MS turns at full stick (turn-speed cap), yaw
 *   mouse_turn_y=1500   same for pitch
 *   mouse_sens=1.0      scales mouse counts entering the queue (1.0 = 1 count -> 1 queued count)
 *   mouse_decay=1.0     seconds; the queued movement fades with this time constant (0 = never)
 *   mouse_deadzone=0.28 stick deflection that just starts the camera turning (game dead zone)
 *   mouse_queue_cap=0   largest queue in counts (0 = unlimited)
 *   mouse_invert_y=0
 *   mouse_left/right/middle/x1/x2/wheel_up/wheel_down=<ps4 button name or empty>
 * Model: mouse counts add to a queue per axis; each poll the stick is deflected as far as needed to
 * drain the queue within the poll, capped at full stick (so a big flick turns at the MS's maximum
 * speed until the queue is empty, a small one turns proportionally); the queue loses what the
 * camera turned and fades by mouse_decay. */
static MouseCfg mcfg; /* set to the defaults on first load (mouse_load) */
static const char *mouse_names[7]={"mouse_left","mouse_right","mouse_middle","mouse_x1","mouse_x2","mouse_wheel_up","mouse_wheel_down"};

static const char *mouse_ini_path(void) {
    static char path[1024];
    if (path[0]) return path;
    const char *cfg=getenv("PS4RT_CONFIG");
    snprintf(path,sizeof(path),"%s",cfg && *cfg ? cfg : "ps4rt.ini");
    char *slash=strrchr(path,'/'); char *bs=strrchr(path,'\\');
    if (bs>slash) slash=bs;
    if (slash) snprintf(slash+1,sizeof(path)-(size_t)(slash+1-path),"ps4rt_mouse.ini");
    else snprintf(path,sizeof(path),"ps4rt_mouse.ini");
    return path;
}
static uint32_t ps_by_name(const char *name) {
    for (size_t i=0;i<sizeof(ps_names)/sizeof(*ps_names);++i) if (!SDL_strcasecmp(name,ps_names[i].name)) return ps_names[i].ps;
    return 0;
}
static void mouse_write_default(const char *path) {
    FILE *f=fopen(path,"w");
    if (!f) return;
    fprintf(f,"# Mouse look settings (edit while the game runs; changes apply within a second).\n"
              "# F8 releases / recaptures the mouse. Buttons take: cross circle square triangle l1 r1 l2 r2 l3 r3 options\n"
              "# touchpad_left touchpad_right up down left right, or nothing.\n"
              "# Keyboard: key_<button>=<key name> (key_cross, key_dpad_up, key_move_up, key_look_left ...), set in the\n"
              "# launcher or the Insert menu (Controls). A missing key keeps its default.\n"
              "# Weapon slots: key_weapon1..8=<key>, weapon1..8=<button>[+<button>], weapon_switch=<button> (added to every slot),\n"
              "# weapon_hold_ms (shortest press), weapon_stagger_ms (switch button first, others after this long).\n"
              "mouse_look=1\n"
              "# counts per second the camera turns at full stick (the MS turn speed cap)\n"
              "mouse_turn_x=1500\nmouse_turn_y=1500\n"
              "# multiplies mouse counts added to the turn queue\n"
              "mouse_sens=1.0\n"
              "# queued turning fades with this time constant in seconds (0 = never fades)\n"
              "mouse_decay=1.0\n"
              "# stick deflection at which the game starts turning (measured: about 0.25-0.3)\n"
              "mouse_deadzone=0.28\n"
              "# largest queue in counts, 0 = unlimited\n"
              "mouse_queue_cap=0\nmouse_invert_y=0\n"
              "# 1 = the game's dead zone is per axis (square), 0 = circular. Try both if diagonals feel bad.\n"
              "mouse_deadzone_mode=1\n"
              "# the game's stick response exponent (1 = linear; >1 if small stick movements turn too little)\n"
              "mouse_curve=1.0\n"
              "# mouse_response=1: drive the stick from the game's measured camera response (below). 0 = the older\n"
              "# deadzone/curve model above. Measured on one MS: no turning below stick 0.30, linear rise to a\n"
              "# first speed plateau at 0.575, a second faster plateau (X only) from about 0.875.\n"
              "mouse_response=1\n"
              "mouse_game_deadzone=0.300\nmouse_game_sat=0.585\nmouse_game_split=0.875\n"
              "# camera speeds in mouse counts per second: yaw first plateau, yaw fast plateau, pitch plateau\n"
              "mouse_speed_low_x=820\nmouse_speed_high_x=1650\nmouse_speed_y=720\n"
              "# queue cap for the pitch axis\n"
              "mouse_queue_cap_y=800\n"
              "mouse_left=r2\nmouse_right=l2\nmouse_middle=r3\nmouse_x1=l1\nmouse_x2=r1\nmouse_wheel_up=\nmouse_wheel_down=\n");
    fclose(f);
}
static void mouse_load(void) {
    static uint64_t last_check; static long long mtime_key=-1; static bool created;
    static bool cfg_init; if (!cfg_init) { cfg_init=true; mcfg=mouse_cfg_default(BTN_R2,BTN_L2,BTN_R3,BTN_L1,BTN_R1); }
    const uint64_t now=now_us();
    if (now-last_check<1000000 && last_check) return;
    last_check=now;
    const char *path=mouse_ini_path();
    struct stat st;
    if (stat(path,&st)!=0) {
        if (created) return;
        created=true; mouse_write_default(path);
        if (stat(path,&st)!=0) return;
    }
    const long long key=(long long)st.st_mtime*1000003LL+(long long)st.st_size;
    if (key==mtime_key) return;
    mtime_key=key;
    FILE *f=fopen(path,"r");
    if (!f) return;
    MouseCfg c=mcfg; char line[256];
    int kb_set[KEY_ACTIONS], sk_set[8], wk_set[8]; uint32_t wb_set[8]; int wb_has[8]={0};
    for (int i=0;i<8;++i) wk_set[i]=-2;   /* -2 = not in the file, 0 = unbound, else a scancode */
    for (int i=0;i<KEY_ACTIONS;++i) kb_set[i]=-2;
    for (int i=0;i<8;++i) sk_set[i]=-2;
    while (fgets(line,sizeof(line),f)) {
        char *t=trim(line);
        if (!*t || *t=='#' || *t==';') continue;
        char *eq=strchr(t,'='); if (!eq) continue;
        *eq=0; const char *k=trim(t), *v=trim(eq+1);
        if (mouse_cfg_set(&c,k,v)) {}
        else if (!strcmp(k,"weapon_switch")) wswitch=*v ? ps_by_name(v) : 0;
        else if (!strcmp(k,"weapon_hold_ms")) { whold_ms=atoi(v); if (whold_ms<0) whold_ms=0; if (whold_ms>1000) whold_ms=1000; }
        else if (!strcmp(k,"weapon_stagger_ms")) { wstagger_ms=atoi(v); if (wstagger_ms<0) wstagger_ms=0; if (wstagger_ms>500) wstagger_ms=500; }
        else if (!strncmp(k,"weapon",6) && k[6]>='1' && k[6]<='8' && !k[7]) {
            const int n=k[6]-'1'; uint32_t b=0; char tmp[128]; snprintf(tmp,sizeof(tmp),"%s",v);
            for (char *part=strtok(tmp,"+,"); part; part=strtok(NULL,"+,")) b|=ps_by_name(trim(part));
            wb_set[n]=b; wb_has[n]=1;
        }
        else if (!strncmp(k,"key_weapon",10) && k[10]>='1' && k[10]<='8' && !k[11]) {
            int sc=0;
            if (*v) { sc=(int)SDL_GetScancodeFromName(v); if (sc<=0) { printf("Runtime: key binding: unknown key '%s' for %s\n",v,k); continue; } }
            wk_set[k[10]-'1']=sc;
        }
        else if (!strncmp(k,"key_",4)) {
            int sc=0;
            if (*v) { sc=(int)SDL_GetScancodeFromName(v); if (sc<=0) { printf("Runtime: key binding: unknown key '%s' for %s\n",v,k); continue; } }
            for (int i=0;i<KEY_ACTIONS;++i) if (!strcmp(k,key_actions[i].key)) kb_set[i]=sc;
            for (int i=0;i<8;++i) if (!strcmp(k,stick_key_names[i])) sk_set[i]=sc;
        }
        else for (int i=0;i<7;++i) if (!strcmp(k,mouse_names[i])) c.btn[i]=*v ? ps_by_name(v) : 0;
    }
    fclose(f);
    mouse_cfg_clamp(&c);
    for (int i=0;i<KEY_ACTIONS;++i) {
        if (kb_set[i]==-2) continue;
        const uint32_t ps=ps_by_name(key_actions[i].ps);
        size_t kept=0;
        for (size_t j=0;j<key_map.count;++j) if (key_map.entries[j].ps!=ps) key_map.entries[kept++]=key_map.entries[j];
        key_map.count=kept;
        if (kb_set[i]>0 && key_map.count<sizeof(key_map.entries)/sizeof(*key_map.entries))
            key_map.entries[key_map.count++]=(ButtonSource){kb_set[i],ps};
    }
    for (int i=0;i<8;++i) if (sk_set[i]!=-2) stick_keys[i]=sk_set[i];
    for (int i=0;i<8;++i) { if (wk_set[i]!=-2) wslot[i].key=wk_set[i]; if (wb_has[i]) wslot[i].buttons=wb_set[i]; }
    mcfg=c;
    printf("Runtime: mouse look: %s, turn %.0f/%.0f counts/s, sens %.2f, decay %.2f s, deadzone %.2f (%s), curve %.2f\n",
           c.look ? "on" : "off",c.turn_x,c.turn_y,c.sens,c.decay,c.deadzone,c.axial ? "axial" : "radial",c.curve);
    printf("Runtime: mouse response model %s (dz %.3f sat %.3f split %.3f, speeds %.0f/%.0f/%.0f counts/s, pitch queue cap %.0f)\n",c.resp ? "on" : "off",c.r_dz,c.r_sat,c.r_split,c.r_low_x,c.r_high_x,c.r_y,c.cap_y);
}
static pthread_mutex_t mouse_lock=PTHREAD_MUTEX_INITIALIZER;
static MouseState ms={0,0,128,128}; /* queue and last stick output */
static uint64_t m_last_us;
static int m_toggled_off, m_f8_was_down, m_wheel_up_ms, m_wheel_down_ms;
/* ---- Stick calibration sweep (F7) ------------------------------------------------------------
 * Feeds the right stick a fixed schedule (a marker pulse, then X levels, Y levels and diagonals,
 * pitch tests start pinned at the bottom limit) so the camera's response can be measured
 * from a recording. Mouse input is ignored while it runs; the steps are printed with their times. */
typedef struct { float dur, x, y; } SweepStep;
static SweepStep sweep[200]; static int sweep_n;
static void sweep_add(float dur, float x, float y) { if (sweep_n<(int)(sizeof(sweep)/sizeof(*sweep))) sweep[sweep_n++]=(SweepStep){dur,x,y}; }
static void sweep_build(void) {
    sweep_n=0;
    sweep_add(0.6f,1,0); sweep_add(2.0f,0,0);                       /* start marker */
    for (int i=0;i<16;++i) {                                        /* X: 0.25 .. 1.00, 3 s each */
        const float v=0.25f+0.05f*(float)i;
        sweep_add(3.0f,(i&1 ? -1.0f : 1.0f)*v,0); sweep_add(1.0f,0,0);
    }
    sweep_add(5.0f,0.6f,0); sweep_add(2.0f,0,0);                    /* ramp test: 0.6 held 5 s */
    /* Y and diagonals: the camera's pitch stops at a limit, so each test starts pinned at the bottom
     * (full stick down for 4 s), goes up for the test and comes back down; only the "up" half is
     * measured. No need to centre the crosshair by hand. */
    sweep_add(4.0f,0,1.0f); sweep_add(0.5f,0,0);
    for (int i=0;i<6;++i) {                                         /* Y: 0.25 .. 1.00, 1.2 s up, 4 s pin */
        const float v=0.25f+0.15f*(float)i;
        sweep_add(1.2f,0,-v); sweep_add(0.5f,0,0); sweep_add(4.0f,0,1.0f); sweep_add(0.5f,0,0);
    }
    static const float ang[2]={0.2915f,0.7854f};                    /* 16.7 and 45 degrees */
    static const float dl[]={0.5f,0.8f,1.0f};
    for (int a=0;a<2;++a) for (int i=0;i<3;++i) {
        sweep_add(1.5f,dl[i]*cosf(ang[a]),-dl[i]*sinf(ang[a])); sweep_add(0.5f,0,0);
        sweep_add(4.0f,0,1.0f); sweep_add(0.5f,0,0);
    }
    sweep_add(0.6f,1,0); sweep_add(1.0f,0,0);                       /* end marker */
}
static int sweep_active; static uint64_t sweep_t0; static int sweep_last=-1;
static int sweep_apply(PadData *d) {
    if (!sweep_active) return 0;
    float t=(float)(now_us()-sweep_t0)/1e6f, acc=0;
    int idx=-1;
    for (int i=0;i<sweep_n;++i) { if (t<acc+sweep[i].dur) { idx=i; break; } acc+=sweep[i].dur; }
    if (idx<0) { sweep_active=0; printf("Runtime: stick sweep finished\n"); return 0; }
    if (idx!=sweep_last) {
        sweep_last=idx;
        if (sweep[idx].x!=0 || sweep[idx].y!=0) printf("Runtime: sweep t=%.2f step %d stick x=%.3f y=%.3f (right = +x, down = +y)\n",t,idx,sweep[idx].x,sweep[idx].y);
    }
    d->right_x=mouse_stick_byte(sweep[idx].x); d->right_y=mouse_stick_byte(sweep[idx].y);
    return 1;
}

/* Measured response model: each axis' camera speed is 0 below the game's dead zone, rises linearly
 * to a plateau at r_sat, and (yaw only) jumps to a faster plateau above r_split. For the desired
 * speed q/dt the stick is chosen by inverting that curve; between the two yaw plateaus the stick
 * switches by threshold and the queue's error feedback averages it. The queue is drained by the
 * speed the quantized stick really produces. */
static void mouse_apply(PadData *d, const bool *k) {
    mouse_load();
    {   static int f7_was_down; const int f7=k && k[SDL_SCANCODE_F7];
        if (f7 && !f7_was_down && !sweep_active) {
            sweep_build(); sweep_active=1; sweep_t0=now_us(); sweep_last=-1;
            printf("Runtime: stick sweep started (%d steps, about %.0f s): keep the mouse still\n",sweep_n,
                   ({ float tt=0; for (int i=0;i<sweep_n;++i) tt+=sweep[i].dur; tt; }));
        }
        f7_was_down=f7; }
    const int wanted=mcfg.look && !m_toggled_off;
    ps4rtgpu_mouse_look_set(wanted);
    const int f8=k && k[SDL_SCANCODE_F8];
    if (f8 && !m_f8_was_down) { m_toggled_off=!m_toggled_off; ps4rtgpu_mouse_look_set(mcfg.look && !m_toggled_off); }
    m_f8_was_down=f8;
    pthread_mutex_lock(&mouse_lock);
    const uint64_t now=now_us();
    float dt=m_last_us ? (float)(now-m_last_us)/1e6f : 0.0f;
    m_last_us=now;
    int dx=0,dy=0,wheel=0;
    const int active=ps4rtgpu_mouse_look_active();
    if (!active) { ms.qx=ms.qy=0; ms.ox=ms.oy=128; m_wheel_up_ms=m_wheel_down_ms=0; ps4rtgpu_mouse_look_take(&dx,&dy,&wheel); pthread_mutex_unlock(&mouse_lock); sweep_apply(d); return; }
    ps4rtgpu_mouse_look_take(&dx,&dy,&wheel);
    if (sweep_active) { ms.qx=ms.qy=0; pthread_mutex_unlock(&mouse_lock); sweep_apply(d); return; }
    if (dt>0.1f) dt=0.1f;
    mouse_model_step(&mcfg,&ms,dt,dx,dy);
    if (ms.ox!=128) d->right_x=ms.ox;
    if (ms.oy!=128) d->right_y=ms.oy;
    pthread_mutex_unlock(&mouse_lock);
    /* buttons: left right middle x1 x2 */
    const uint32_t mb=SDL_GetMouseState(NULL,NULL);
    const uint32_t masks[5]={SDL_BUTTON_LMASK,SDL_BUTTON_RMASK,SDL_BUTTON_MMASK,SDL_BUTTON_X1MASK,SDL_BUTTON_X2MASK};
    for (int i=0;i<5;++i) if ((mb & masks[i]) && mcfg.btn[i]) d->buttons|=mcfg.btn[i];
    if (wheel>0) m_wheel_up_ms=60; else if (wheel<0) m_wheel_down_ms=60;
    if (dt>0) { m_wheel_up_ms-=(int)(dt*1000); m_wheel_down_ms-=(int)(dt*1000); }
    if (m_wheel_up_ms>0 && mcfg.btn[5]) d->buttons|=mcfg.btn[5];
    if (m_wheel_down_ms>0 && mcfg.btn[6]) d->buttons|=mcfg.btn[6];
    if (d->buttons & BTN_L2) d->l2=255;
    if (d->buttons & BTN_R2) d->r2=255;
}

static void key_axis(uint8_t *axis, bool negative, bool positive) {
    if (negative || positive) *axis=(uint8_t)(128-(negative ? 128 : 0)+(positive ? 127 : 0));
}
static void sample_host(PadData *d) {
    memset(d,0,sizeof(*d));
    d->left_x=d->left_y=d->right_x=d->right_y=128;
    d->orientation[3]=1.0f;
    d->connected=1; d->connected_count=connected_count ? connected_count : 1;
    d->timestamp=now_us();
    SDL_Gamepad *g=current_gamepad();
    if (ps4rtgpu_overlay_captures_input()) return; /* settings menu open: neutral input */
    const bool *k=SDL_WasInit(SDL_INIT_VIDEO) ? SDL_GetKeyboardState(NULL) : NULL;
    load_button_maps();
    if (g) {
        for (size_t i=0;i<pad_map.count;++i)
            if (SDL_GetGamepadButton(g,(SDL_GamepadButton)pad_map.entries[i].source)) d->buttons|=pad_map.entries[i].ps;
        const uint32_t mapped=d->buttons;
        d->left_x=axis(SDL_GetGamepadAxis(g,SDL_GAMEPAD_AXIS_LEFTX)); d->left_y=axis(SDL_GetGamepadAxis(g,SDL_GAMEPAD_AXIS_LEFTY));
        d->right_x=axis(SDL_GetGamepadAxis(g,SDL_GAMEPAD_AXIS_RIGHTX)); d->right_y=axis(SDL_GetGamepadAxis(g,SDL_GAMEPAD_AXIS_RIGHTY));
        d->l2=trigger(SDL_GetGamepadAxis(g,SDL_GAMEPAD_AXIS_LEFT_TRIGGER)); d->r2=trigger(SDL_GetGamepadAxis(g,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
        if (d->l2>30) d->buttons|=BTN_L2;
        if (d->r2>30) d->buttons|=BTN_R2;
        if (mapped & BTN_L2) d->l2=255;
        if (mapped & BTN_R2) d->r2=255;
        if (SDL_GetNumGamepadTouchpads(g)>0) {
            const int fingers=SDL_GetNumGamepadTouchpadFingers(g,0);
            for (int finger=0;finger<fingers && d->touch_count<2;++finger) {
                bool down=false;
                float x=0, y=0;
                if (SDL_GetGamepadTouchpadFinger(g,0,finger,&down,&x,&y,NULL) && down) {
                    d->touches[d->touch_count++]=(PadTouch){.x=touch_axis(x,1919),
                        .y=touch_axis(y,942),.id=(uint8_t)finger};
                }
            }
        }
    }
    /* The keyboard works with or without a gamepad: its buttons add to the pad's, and a stick
     * follows the keys only while one of them is held. */
    if (!k) return;
    for (size_t i=0;i<key_map.count;++i) if (k[key_map.entries[i].source]) d->buttons|=key_map.entries[i].ps;
    weapon_apply(d,k);
    if (d->buttons & BTN_L2) d->l2=255;
    if (d->buttons & BTN_R2) d->r2=255;
#define SK(i) (stick_keys[i]>0 && k[stick_keys[i]])
    key_axis(&d->left_x,SK(0),SK(1));
    key_axis(&d->left_y,SK(2),SK(3));
    key_axis(&d->right_x,SK(4),SK(5));
    key_axis(&d->right_y,SK(6),SK(7));
#undef SK
    mouse_apply(d,k);
}

/* PS4RT_PAD_FILE=<file>: scripted input for automated runs. The file holds whitespace-separated
 * tokens, re-read when it changes: button names (cross circle square triangle l1 r1 l2 r2 l3 r3
 * options touchpad touchpad_left touchpad_right up down left right) are held while listed;
 * touchpad is the left side; lx= ly= rx= ry= (0..255) override the sticks. An empty file
 * releases everything. */
static struct { uint32_t buttons; int stick[4]; } injected={0,{-1,-1,-1,-1}};
static int replay_armed;      /* 1 while a PS4RT_PAD_REPLAY recording plays, 2 once it ended */
static uint64_t replay_start; /* 0: (re)start at the next sample */
static void read_inject(void) {
    static const char *path; static int checked; static uint64_t last_check; static struct timespec mtime;
    if (!checked) { path=getenv("PS4RT_PAD_FILE"); checked=1; }
    if (!path || !*path) return;
    uint64_t now=now_us();
    if (now-last_check<20000) return;
    last_check=now;
    struct stat st;
    if (stat(path,&st)!=0) return;
#ifdef _WIN32
    /* Whole-second mtimes: the size tells two edits within a second apart. */
    if (st.st_mtime==mtime.tv_sec && st.st_size==mtime.tv_nsec) return;
    mtime=(struct timespec){st.st_mtime,(long)st.st_size};
#else
    if (st.st_mtim.tv_sec==mtime.tv_sec && st.st_mtim.tv_nsec==mtime.tv_nsec) return;
    mtime=st.st_mtim;
#endif
    FILE *f=fopen(path,"r");
    if (!f) return;
    static const char *sticks[]={"lx=","ly=","rx=","ry="};
    injected.buttons=0;
    for (int i=0;i<4;++i) injected.stick[i]=-1;
    char token[64];
    while (fscanf(f,"%63s",token)==1) {
        if (!strcmp(token,"replay") && replay_armed!=1) { replay_armed=1; replay_start=0; } /* PS4RT_PAD_REPLAY */
        for (size_t i=0;i<sizeof(ps_names)/sizeof(*ps_names);++i) if (!strcmp(token,ps_names[i].name)) injected.buttons|=ps_names[i].ps;
        for (int i=0;i<4;++i) if (!strncmp(token,sticks[i],3)) { int v=atoi(token+3); injected.stick[i]=v<0 ? 0 : v>255 ? 255 : v; }
    }
    fclose(f);
    printf("Runtime: pad file: buttons 0x%x sticks %d %d %d %d\n",injected.buttons,
           injected.stick[0],injected.stick[1],injected.stick[2],injected.stick[3]);
}
/* PS4RT_PAD_RECORD=<file>: F9 starts and stops recording the pad state (gamepad or keyboard) with
 * the time since F9; PS4RT_PAD_REPLAY=<file> plays such a recording back, started by the token
 * "replay" in PS4RT_PAD_FILE (scripted tests repeat a route the player ran once). Lines: ms buttons
 * lx ly rx ry l2 r2, written when the state changes. */
typedef struct { uint32_t ms, buttons; uint8_t axes[4], l2, r2; } PadSample;
static FILE *record_file;
static uint64_t record_start;
static PadSample record_last;
static void savewatch_key(void) {
    static int was_down;
    if (!sdl_ready) return;
    const bool *k=SDL_GetKeyboardState(NULL);
    const int down=k && k[SDL_SCANCODE_F10];
    if (down && !was_down) runtime_savewatch_toggle();
    was_down=down;
}
static void record_sample(const PadData *d) {
    static const char *path; static int checked, f9_was_down;
    if (!checked) { path=getenv("PS4RT_PAD_RECORD"); checked=1; }
    if (!path || !*path || !sdl_ready) return;
    const bool *k=SDL_GetKeyboardState(NULL);
    const int f9=k && k[SDL_SCANCODE_F9];
    if (f9 && !f9_was_down) {
        if (record_file) {
            fclose(record_file); record_file=NULL;
            printf("Runtime: pad recording stopped (%s)\n",path);
        } else if ((record_file=fopen(path,"w"))) {
            record_start=now_us();
            memset(&record_last,0xff,sizeof(record_last));
            printf("Runtime: pad recording started (%s, F9 stops)\n",path);
        }
    }
    f9_was_down=f9;
    if (!record_file) return;
    PadSample s={(uint32_t)((now_us()-record_start)/1000),d->buttons,
                 {d->left_x,d->left_y,d->right_x,d->right_y},d->l2,d->r2};
    if (s.buttons==record_last.buttons && !memcmp(s.axes,record_last.axes,4) &&
        s.l2==record_last.l2 && s.r2==record_last.r2) return;
    record_last=s;
    fprintf(record_file,"%u %u %u %u %u %u %u %u\n",s.ms,s.buttons,s.axes[0],s.axes[1],s.axes[2],
            s.axes[3],s.l2,s.r2);
    fflush(record_file);
}
static PadSample *replay; static size_t replay_count, replay_next;
static void replay_sample(PadData *d) {
    if (!replay_armed) return;
    if (!replay_start) {
        static int loaded;
        if (!loaded) {
            loaded=1;
            const char *path=getenv("PS4RT_PAD_REPLAY");
            FILE *f=path ? fopen(path,"r") : NULL;
            PadSample s; unsigned v[8]; size_t cap=0;
            while (f && fscanf(f,"%u %u %u %u %u %u %u %u",&v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&v[6],&v[7])==8) {
                s=(PadSample){v[0],v[1],{(uint8_t)v[2],(uint8_t)v[3],(uint8_t)v[4],(uint8_t)v[5]},(uint8_t)v[6],(uint8_t)v[7]};
                if (replay_count==cap && !(replay=realloc(replay,(cap=cap ? cap*2 : 1024)*sizeof(*replay)))) break;
                replay[replay_count++]=s;
            }
            if (f) fclose(f);
            printf("Runtime: pad replay of %zu samples from %s\n",replay_count,path ? path : "(unset)");
        }
        replay_start=now_us();
        replay_next=0;
    }
    const uint32_t ms=(uint32_t)((now_us()-replay_start)/1000);
    while (replay_next<replay_count && replay[replay_next].ms<=ms) ++replay_next;
    if (!replay_next) return;
    if (replay_next==replay_count && ms>replay[replay_count-1].ms+500) {
        if (replay_armed==1) { puts("Runtime: pad replay finished"); replay_armed=2; }
        return;
    }
    const PadSample *s=&replay[replay_next-1];
    d->buttons=s->buttons;
    d->left_x=s->axes[0]; d->left_y=s->axes[1]; d->right_x=s->axes[2]; d->right_y=s->axes[3];
    d->l2=s->l2; d->r2=s->r2;
}
/* After the menu or the text dialog closes, buttons still held (the Cross that accepted a
 * name) stay hidden until released: the game would take them as a new press. */
static int hold_after_capture;
static void sample(PadData *d) {
    sample_host(d);
    if (ps4rtgpu_overlay_captures_input()) { hold_after_capture=1; click.side=-1; return; }
    savewatch_key();
    record_sample(d); /* the click sources as they are: a replay plays them again */
    read_inject();
    replay_sample(d);
    d->buttons|=injected.buttons;
    if (injected.buttons & BTN_L2) d->l2=255;
    if (injected.buttons & BTN_R2) d->r2=255;
    uint8_t *axes[4]={&d->left_x,&d->left_y,&d->right_x,&d->right_y};
    for (int i=0;i<4;++i) if (injected.stick[i]>=0) *axes[i]=(uint8_t)injected.stick[i];
    play_click(d);
    if (hold_after_capture) {
        if (d->buttons) d->buttons=0;
        else hold_after_capture=0;
    }
}

static ABI int32_t pad_init(void) { pthread_mutex_lock(&lock); initialized=1; pthread_mutex_unlock(&lock); return 0; }
static ABI int32_t pad_open(int32_t user, int32_t type, int32_t index, const void *param) {
    (void)param;
    if (!initialized) return ERR_NOT_INITIALIZED;
    if (user!=1) return ERR_INVALID_ARG;
    if (type!=0 && type!=2) return ERR_INVALID_ARG; /* standard / special port */
    if (index) return ERR_INVALID_ARG;
    pthread_mutex_lock(&lock);
    int already=opened; opened=1;
    pthread_mutex_unlock(&lock);
    if (already) return ERR_ALREADY_OPENED;
    puts("Runtime: pad opened for user 1 (SDL gamepad or keyboard)");
    return PAD_HANDLE;
}
static ABI int32_t pad_close(int32_t handle) {
    if (handle!=PAD_HANDLE || !opened) return ERR_INVALID_HANDLE;
    opened=0; return 0;
}
static ABI int32_t pad_read_state(int32_t handle, PadData *data) {
    if (handle!=PAD_HANDLE || !opened) return ERR_INVALID_HANDLE;
    if (!data) return ERR_INVALID_ARG;
    pthread_mutex_lock(&lock);
    sample(data); ++reads;
    pthread_mutex_unlock(&lock);
    return 0;
}
/* Buffered read: the port samples once per call, so one entry is returned. */
static ABI int32_t pad_read(int32_t handle, PadData *data, int32_t count) {
    if (handle!=PAD_HANDLE || !opened) return ERR_INVALID_HANDLE;
    if (!data || count<1 || count>64) return ERR_INVALID_ARG;
    pad_read_state(handle,data);
    return 1;
}
static ABI int32_t pad_info(int32_t handle, ControllerInfo *info) {
    if (handle!=PAD_HANDLE || !opened) return ERR_INVALID_HANDLE;
    if (!info) return ERR_INVALID_ARG;
    memset(info,0,sizeof(*info));
    info->pixel_density=44.86f; info->resolution_x=1920; info->resolution_y=943;
    info->dead_zone_left=info->dead_zone_right=2;
    info->connection_type=0; info->connected=1; info->device_class=0;
    pthread_mutex_lock(&lock);
    current_gamepad();
    info->connected_count=connected_count ? connected_count : 1;
    pthread_mutex_unlock(&lock);
    return 0;
}
static ABI int32_t pad_vibration(int32_t handle, const uint8_t *param) {
    if (handle!=PAD_HANDLE || !opened) return ERR_INVALID_HANDLE;
    if (!param) return ERR_INVALID_ARG;
    pthread_mutex_lock(&lock);
    SDL_Gamepad *g=current_gamepad();
    /* The menu's vibration strength scales the game's motors (0: off). */
    const unsigned percent=(unsigned)ps4rtgpu_vibration_percent();
    if (g) SDL_RumbleGamepad(g,(uint16_t)(param[0]*257u*percent/100u),(uint16_t)(param[1]*257u*percent/100u),1000);
    pthread_mutex_unlock(&lock);
    return 0;
}
static ABI int32_t pad_ok_handle(int32_t handle) { return handle==PAD_HANDLE && opened ? 0 : ERR_INVALID_HANDLE; }
static ABI int32_t pad_ok_handle_flag(int32_t handle, uint8_t flag) { (void)flag; return pad_ok_handle(handle); }

static ABI int32_t pad_light_bar(int32_t handle, const uint8_t *rgb) {
    if (handle!=PAD_HANDLE || !opened) return ERR_INVALID_HANDLE;
    (void)rgb; return 0;
}
static const RuntimeExport exports[]={
    {"scePadSetLightBar",pad_light_bar}, {"scePadResetLightBar",pad_ok_handle},
    {"scePadInit",pad_init}, {"scePadOpen",pad_open}, {"scePadClose",pad_close},
    {"scePadReadState",pad_read_state}, {"scePadRead",pad_read},
    {"scePadGetControllerInformation",pad_info}, {"scePadSetVibration",pad_vibration},
    {"scePadResetOrientation",pad_ok_handle},
    {"scePadSetAngularVelocityDeadbandState",pad_ok_handle_flag}, {"scePadSetTiltCorrectionState",pad_ok_handle_flag},
    {"scePadSetMotionSensorState",pad_ok_handle_flag},
};
uintptr_t runtime_pad_resolve(const char *name) { return RUNTIME_LOOKUP(exports,name); }
void runtime_pad_report(void) { printf("Runtime: pad reads=%zu, gamepad=%s\n",reads,gamepad ? SDL_GetGamepadName(gamepad) : "none"); }

/* probe --gamepad: runs before any game state exists, so it uses its own SDL gamepad and leaves
 * the lock and statics above alone. run.py parses this line; keep the format. */
static const char *label_name(SDL_GamepadButtonLabel label) {
    switch (label) {
    case SDL_GAMEPAD_BUTTON_LABEL_A: return "a";
    case SDL_GAMEPAD_BUTTON_LABEL_B: return "b";
    case SDL_GAMEPAD_BUTTON_LABEL_X: return "x";
    case SDL_GAMEPAD_BUTTON_LABEL_Y: return "y";
    case SDL_GAMEPAD_BUTTON_LABEL_CROSS: return "cross";
    case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: return "circle";
    case SDL_GAMEPAD_BUTTON_LABEL_SQUARE: return "square";
    case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE: return "triangle";
    default: return "unknown";
    }
}
int runtime_pad_describe(void) {
    SDL_Gamepad *g=NULL;
    const bool ready=SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    /* No event loop yet, so pads show up only after a pump: poll for about 300 ms. */
    for (int waited=0; ready && !g && waited<30; ++waited) {
        SDL_PumpEvents();
        int count=0;
        SDL_JoystickID *ids=SDL_GetGamepads(&count);
        if (ids && count>0) g=SDL_OpenGamepad(ids[0]);
        SDL_free(ids);
        if (!g) SDL_Delay(10);
    }
    if (!g) puts("Gamepad: none");
    else {
        const char *type=SDL_GetGamepadStringForType(SDL_GetGamepadType(g));
        const char *name=SDL_GetGamepadName(g);
        printf("Gamepad: type=%s south=%s east=%s west=%s north=%s name=%s\n",
               type ? type : "unknown",
               label_name(SDL_GetGamepadButtonLabel(g,SDL_GAMEPAD_BUTTON_SOUTH)),
               label_name(SDL_GetGamepadButtonLabel(g,SDL_GAMEPAD_BUTTON_EAST)),
               label_name(SDL_GetGamepadButtonLabel(g,SDL_GAMEPAD_BUTTON_WEST)),
               label_name(SDL_GetGamepadButtonLabel(g,SDL_GAMEPAD_BUTTON_NORTH)),
               name ? name : "unknown");
        SDL_CloseGamepad(g);
    }
    if (ready) SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    return g!=NULL;
}
