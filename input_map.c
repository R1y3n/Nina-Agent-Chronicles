#include "input_map.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <strings.h>

enum { B_L3 = 0x2, B_R3 = 0x4, B_OPTIONS = 0x8, B_UP = 0x10, B_RIGHT = 0x20, B_DOWN = 0x40, B_LEFT = 0x80, B_L2 = 0x100, B_R2 = 0x200, B_L1 = 0x400, B_R1 = 0x800,
       B_TRIANGLE = 0x1000, B_CIRCLE = 0x2000, B_CROSS = 0x4000, B_SQUARE = 0x8000, B_TOUCH = 0x100000 };
static const struct { const char *n; int c; } KEYS[] = {
 {"ESC",1},{"1",2},{"2",3},{"3",4},{"4",5},{"5",6},{"6",7},{"7",8},{"8",9},{"9",10},{"0",11},{"MINUS",12},{"EQUALS",13},{"BACK",14},{"BACKSPACE",14},{"TAB",15},
 {"Q",0x10},{"W",0x11},{"E",0x12},{"R",0x13},{"T",0x14},{"Y",0x15},{"U",0x16},{"I",0x17},{"O",0x18},{"P",0x19},{"LBRACKET",0x1A},{"RBRACKET",0x1B},
 {"RETURN",0x1C},{"ENTER",0x1C},{"LCTRL",0x1D},{"LCONTROL",0x1D},{"A",0x1E},{"S",0x1F},{"D",0x20},{"F",0x21},{"G",0x22},{"H",0x23},{"J",0x24},{"K",0x25},{"L",0x26},
 {"SEMICOLON",0x27},{"APOSTROPHE",0x28},{"GRAVE",0x29},{"LSHIFT",0x2A},{"BACKSLASH",0x2B},{"Z",0x2C},{"X",0x2D},{"C",0x2E},{"V",0x2F},{"B",0x30},{"N",0x31},{"M",0x32},
 {"COMMA",0x33},{"PERIOD",0x34},{"SLASH",0x35},{"RSHIFT",0x36},{"LALT",0x38},{"SPACE",0x39},{"CAPSLOCK",0x3A},
 {"F1",0x3B},{"F2",0x3C},{"F3",0x3D},{"F4",0x3E},{"F5",0x3F},{"F6",0x40},{"F7",0x41},{"F8",0x42},{"F9",0x43},{"F10",0x44},{"F11",0x57},{"F12",0x58},
 {"NUMPAD7",0x47},{"NUMPAD8",0x48},{"NUMPAD9",0x49},{"NUMPAD4",0x4B},{"NUMPAD5",0x4C},{"NUMPAD6",0x4D},{"NUMPAD1",0x4F},{"NUMPAD2",0x50},{"NUMPAD3",0x51},{"NUMPAD0",0x52},
 {"RCTRL",0x9D},{"RCONTROL",0x9D},{"RALT",0xB8},{"HOME",0xC7},{"UP",0xC8},{"PGUP",0xC9},{"LEFT",0xCB},{"RIGHT",0xCD},{"END",0xCF},{"DOWN",0xD0},{"PGDN",0xD1},{"INSERT",0xD2},{"DELETE",0xD3},
};
int input_key_from_name(const char *name) { for (unsigned i = 0; i < sizeof KEYS / sizeof *KEYS; i++) if (!strcasecmp(KEYS[i].n, name)) return KEYS[i].c; return -1; }

static void add(InputMap *m, uint32_t mask, int analog, int k1, int k2, int mb, int wheel) {
    if (m->nb >= 24) return;
    Binding *b = &m->b[m->nb++]; b->pad_mask = mask; b->analog = (int8_t)analog; b->keys[0] = (uint8_t)k1; b->keys[1] = (uint8_t)k2; b->mouse_btn = (int8_t)mb; b->wheel = (int8_t)wheel;
}
void input_map_defaults(InputMap *m) {
    memset(m, 0, sizeof *m);
    m->look_sens = 900.f; m->look_deadzone = 0.12f; m->look_curve = 1.6f; m->move_threshold = 0.38f;
    add(m, B_CROSS, 0, 0x39, 0x1C, -1, 0);     /* jump / menu accept */
    add(m, B_CIRCLE, 0, 0x1D, 0, -1, 0);       /* crouch */
    add(m, B_SQUARE, 0, 0x12, 0, -1, 0);       /* use */
    add(m, B_TRIANGLE, 0, 0x13, 0, -1, 0);     /* reload */
    add(m, B_L1, 0, 0, 0, -1, -1);             /* previous weapon (wheel down) */
    add(m, B_R1, 0, 0, 0, -1, +1);             /* next weapon (wheel up) */
    add(m, B_L2, 1, 0, 0, 1, 0);               /* secondary fire / aim */
    add(m, B_R2, 2, 0, 0, 0, 0);               /* fire / menu click */
    add(m, B_L3, 0, 0x2A, 0, -1, 0);           /* run modifier */
    add(m, B_R3, 0, 0x2E, 0, -1, 0);
    add(m, B_OPTIONS, 0, 0x01, 0, -1, 0);      /* menu */
    add(m, B_TOUCH, 0, 0x0F, 0, -1, 0);        /* scores / map */
    add(m, B_UP, 0, 0xC8, 0, -1, 0); add(m, B_DOWN, 0, 0xD0, 0, -1, 0); add(m, B_LEFT, 0, 0xCB, 0, -1, 0); add(m, B_RIGHT, 0, 0xCD, 0, -1, 0);
}
/* cfg lines:  cross = SPACE,RETURN   r2 = MOUSE1   r1 = WHEEL_UP   look_sensitivity = 900 ...  '#' starts a comment */
static const struct { const char *n; uint32_t m; } BTN[] = { {"cross",B_CROSS},{"circle",B_CIRCLE},{"square",B_SQUARE},{"triangle",B_TRIANGLE},{"l1",B_L1},{"r1",B_R1},{"l2",B_L2},{"r2",B_R2},
    {"l3",B_L3},{"r3",B_R3},{"options",B_OPTIONS},{"touchpad",B_TOUCH},{"up",B_UP},{"down",B_DOWN},{"left",B_LEFT},{"right",B_RIGHT} };
int input_map_load(InputMap *m, const char *path) {
    FILE *f = fopen(path, "r"); if (!f) return -1; char line[256]; int applied = 0;
    while (fgets(line, sizeof line, f)) {
        char *h = strchr(line, '#'); if (h) *h = 0;
        char *eq = strchr(line, '='); if (!eq) continue; *eq = 0; char *k = line, *v = eq + 1;
        while (isspace((unsigned char)*k)) k++; for (char *e = k + strlen(k); e > k && isspace((unsigned char)e[-1]); ) *--e = 0;
        while (isspace((unsigned char)*v)) v++; for (char *e = v + strlen(v); e > v && isspace((unsigned char)e[-1]); ) *--e = 0;
        if (!strcasecmp(k, "look_sensitivity")) { m->look_sens = (float)atof(v); applied++; continue; }
        if (!strcasecmp(k, "look_deadzone")) { m->look_deadzone = (float)atof(v); applied++; continue; }
        if (!strcasecmp(k, "look_curve")) { m->look_curve = (float)atof(v); applied++; continue; }
        if (!strcasecmp(k, "move_threshold")) { m->move_threshold = (float)atof(v); applied++; continue; }
        for (unsigned i = 0; i < sizeof BTN / sizeof *BTN; i++) if (!strcasecmp(k, BTN[i].n)) {
            Binding *b = NULL; for (int j = 0; j < m->nb; j++) if (m->b[j].pad_mask == BTN[i].m) b = &m->b[j];
            if (!b) { add(m, BTN[i].m, BTN[i].m == B_L2 ? 1 : BTN[i].m == B_R2 ? 2 : 0, 0, 0, -1, 0); b = &m->b[m->nb - 1]; }
            b->keys[0] = b->keys[1] = 0; b->mouse_btn = -1; b->wheel = 0; int nk = 0;
            for (char *tok = strtok(v, ", "); tok; tok = strtok(NULL, ", ")) {
                if (!strncasecmp(tok, "MOUSE", 5)) b->mouse_btn = (int8_t)(atoi(tok + 5) - 1);
                else if (!strcasecmp(tok, "WHEEL_UP")) b->wheel = 1; else if (!strcasecmp(tok, "WHEEL_DOWN")) b->wheel = -1;
                else if (!strcasecmp(tok, "NONE")) {} else { int c = input_key_from_name(tok); if (c > 0 && nk < 2) b->keys[nk++] = (uint8_t)c; }
            }
            applied++; break;
        }
    }
    fclose(f); return applied;
}
static float curve(float dev, float dz, float pw) { if (dev > 1) dev = 1; if (dev < -1) dev = -1; float a = fabsf(dev); if (a <= dz) return 0; a = (a - dz) / (1 - dz); a = powf(a, pw); return dev < 0 ? -a : a; }
void input_map_apply(InputMap *m, const PadSample *p, int dt_ms, InputState *st) {
    memset(st->key, 0, sizeof st->key); memset(st->mbtn, 0, sizeof st->mbtn);
    float lx = (p->lx - 128) / 127.f, ly = (p->ly - 128) / 127.f, rx = (p->rx - 128) / 127.f, ry = (p->ry - 128) / 127.f;
    if (ly < -m->move_threshold) st->key[0x11] = 0x80;            /* W  (stick up = low y) */
    if (ly >  m->move_threshold) st->key[0x1F] = 0x80;            /* S */
    if (lx < -m->move_threshold) st->key[0x1E] = 0x80;            /* A */
    if (lx >  m->move_threshold) st->key[0x20] = 0x80;            /* D */
    for (int i = 0; i < m->nb; i++) {
        Binding *b = &m->b[i]; int down = b->analog == 1 ? p->l2 > 64 : b->analog == 2 ? p->r2 > 64 : (p->buttons & b->pad_mask) != 0;
        int was = b->analog == 1 ? m->prev_l2 > 64 : b->analog == 2 ? m->prev_r2 > 64 : (m->prev_buttons & b->pad_mask) != 0;
        if (down) { for (int k = 0; k < 2; k++) if (b->keys[k]) st->key[b->keys[k]] = 0x80; if (b->mouse_btn >= 0 && b->mouse_btn < 8) st->mbtn[b->mouse_btn] = 0x80; }
        if (down && !was && b->wheel) st->wheel += b->wheel * 120;
    }
    m->prev_buttons = p->buttons; m->prev_l2 = p->l2; m->prev_r2 = p->r2;
    m->acc_x += curve(rx, m->look_deadzone, m->look_curve) * m->look_sens * dt_ms / 1000.f;
    m->acc_y += curve(ry, m->look_deadzone, m->look_curve) * m->look_sens * dt_ms / 1000.f;
    int ix = (int)m->acc_x, iy = (int)m->acc_y; m->acc_x -= ix; m->acc_y -= iy; st->dx += ix; st->dy += iy;
}
#ifdef INPUT_MAP_TEST
#include <assert.h>
int main(void) {
    InputMap m; input_map_defaults(&m); InputState s; memset(&s, 0, sizeof s); PadSample p = { 0, 128, 128, 128, 128, 0, 0 };
    input_map_apply(&m, &p, 16, &s); for (int i = 0; i < 256; i++) assert(!s.key[i]); assert(!s.dx && !s.dy);                       /* idle: nothing */
    p.ly = 0; p.lx = 255; input_map_apply(&m, &p, 16, &s); assert(s.key[0x11] == 0x80 && s.key[0x20] == 0x80 && !s.key[0x1F] && !s.key[0x1E]);   /* up+right = W+D */
    p.ly = 128; p.lx = 128; p.buttons = B_CROSS | B_UP; p.r2 = 255; input_map_apply(&m, &p, 16, &s);
    assert(s.key[0x39] == 0x80 && s.key[0x1C] == 0x80 && s.key[0xC8] == 0x80 && s.mbtn[0] == 0x80 && !s.mbtn[1]);                 /* cross=space+enter, dpad up, R2=mouse1 */
    p.buttons = B_R1; p.r2 = 0; input_map_apply(&m, &p, 16, &s); assert(s.wheel == 120); input_map_apply(&m, &p, 16, &s); assert(s.wheel == 120);   /* wheel is edge-triggered */
    p.buttons = 0; p.rx = 255; p.ry = 0; s.dx = s.dy = 0; for (int i = 0; i < 100; i++) input_map_apply(&m, &p, 10, &s);                  /* 1 s full right+up */
    assert(s.dx > 800 && s.dx <= 900 && s.dy < -800 && s.dy >= -900);
    p.rx = 128 + 10; p.ry = 128; s.dx = 0; for (int i = 0; i < 100; i++) input_map_apply(&m, &p, 10, &s); assert(s.dx == 0);          /* inside the deadzone */
    FILE *f = fopen("/tmp/t.cfg", "w"); fprintf(f, "# test\ncross = F,MOUSE2\nr1 = WHEEL_DOWN\nlook_sensitivity = 450\n"); fclose(f);
    InputMap m2; input_map_defaults(&m2); assert(input_map_load(&m2, "/tmp/t.cfg") == 3); assert(m2.look_sens == 450.f);
    InputState s2; memset(&s2, 0, sizeof s2); PadSample q = { B_CROSS | B_R1, 128, 128, 128, 128, 0, 0 }; input_map_apply(&m2, &q, 16, &s2);
    assert(s2.key[0x21] == 0x80 && !s2.key[0x39] && s2.mbtn[1] == 0x80 && s2.wheel == -120);
    printf("input_map: all tests passed\n"); return 0;
}
#endif
