/* gfx_soft.c - CPU rasteriser backend for gfx.h (Direct3D7 pixel pipeline emulated in software).
 * Why it exists: it needs no GPU API, so it is the guaranteed, testable-on-any-host path (PC rig, PS4 via sceVideoOut).  Strong symbols override
 * the weak ones in gfx_null.c.  Covers: triangle list/strip/fan (+ lines/points), z-buffer + all D3DCMP funcs, 3 texture stages with the D3DTOP_*
 * combiners, diffuse/specular/tfactor/current args (+COMPLEMENT/ALPHAREPLICATE), D3DBLEND factors, alpha test, table fog, culling, wrap/mirror/clamp,
 * point or bilinear sampling.  Output: ARGB8888 framebuffer handed to gfx_soft_present() (weak; ps4_video.c scales it to the display).
 * Debug: RT_GFX_DUMP=<prefix> writes <prefix>NNNN.ppm every RT_GFX_DUMP_EVERY (default 1) presented frames.
 * Perf notes (not done yet): pixel loop is scalar float; a render thread fed by a vertex command queue is the obvious next step. */
#include "gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
typedef struct { int w, h; uint32_t *px; } Tex;
static Tex *texs; static int ntex_cap;
static uint32_t *fb; static float *zb; static int W = 640, H = 480, frames;
__attribute__((weak)) void gfx_soft_present(const uint32_t *argb, int w, int h) { (void)argb; (void)w; (void)h; }

int gfx_init(int w, int h) { gfx_set_mode(w, h); return 0; }
void gfx_set_mode(int w, int h) { free(fb); free(zb); W = w; H = h; fb = calloc((size_t)w * h, 4); zb = malloc((size_t)w * h * sizeof *zb); for (size_t i = 0; i < (size_t)w * h; i++) zb[i] = 1.0f; }
uint32_t gfx_tex_create(int w, int h) { for (int i = 1; i < ntex_cap; i++) if (!texs[i].px && !texs[i].w) { texs[i].w = w; texs[i].h = h; texs[i].px = calloc((size_t)w * h, 4); return (uint32_t)i; }
    int n = ntex_cap ? ntex_cap * 2 : 64; texs = realloc(texs, (size_t)n * sizeof *texs); memset(texs + ntex_cap, 0, (size_t)(n - ntex_cap) * sizeof *texs); if (!ntex_cap) ntex_cap = 1; int id = ntex_cap; ntex_cap = n;
    texs[id].w = w; texs[id].h = h; texs[id].px = calloc((size_t)w * h, 4); return (uint32_t)id; }
void gfx_tex_upload(uint32_t t, const uint8_t *p, int w, int h) { if (t >= (uint32_t)ntex_cap || !texs[t].w) return; Tex *x = &texs[t];
    if (x->w != w || x->h != h) { free(x->px); x->w = w; x->h = h; x->px = malloc((size_t)w * h * 4); }
    for (int i = 0; i < w * h; i++) x->px[i] = ((uint32_t)p[4*i+3] << 24) | ((uint32_t)p[4*i] << 16) | ((uint32_t)p[4*i+1] << 8) | p[4*i+2]; }     /* RGBA bytes -> ARGB word */
void gfx_tex_delete(uint32_t t) { if (t && t < (uint32_t)ntex_cap) { free(texs[t].px); texs[t] = (Tex){ 0, 0, NULL }; } }
void gfx_begin_scene(void) {}
void gfx_end_scene(void) {}
static void vp_rect(const GfxState *s, int *x0, int *y0, int *x1, int *y1) {
    *x0 = 0; *y0 = 0; *x1 = W; *y1 = H;
    if (s && s->vp_w > 0 && s->vp_h > 0) { *x0 = s->vp_x > 0 ? s->vp_x : 0; *y0 = s->vp_y > 0 ? s->vp_y : 0; *x1 = s->vp_x + s->vp_w < W ? s->vp_x + s->vp_w : W; *y1 = s->vp_y + s->vp_h < H ? s->vp_y + s->vp_h : H; } }
void gfx_clear(int flags, uint32_t argb, float z, const GfxState *s) {
    int x0, y0, x1, y1; vp_rect(s, &x0, &y0, &x1, &y1);
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) { if (flags & 1) fb[y * W + x] = argb | 0xFF000000u; if (flags & 2) zb[y * W + x] = z; } }

typedef struct { float r, g, b, a; } C4;
static inline float sat(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static C4 unpack(uint32_t c) { return (C4){ ((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f, (c >> 24) / 255.f }; }
static float addr(float u, int mode) {
    switch (mode) { case 2: { float f = u - 2 * floorf(u / 2); return f > 1 ? 2 - f : f; } case 3: case 4: return sat(u); default: return u - floorf(u); } }
static C4 texel(const Tex *t, int x, int y) { return unpack(t->px[y * t->w + x]); }
static C4 sample(const Tex *t, const GfxState *s, int st, float u, float v) {
    u = addr(u, s->stage[st].addr_u ? s->stage[st].addr_u : 1); v = addr(v, s->stage[st].addr_v ? s->stage[st].addr_v : 1);
    if (s->stage[st].mag != 2 && s->stage[st].mag != 3) { int x = (int)(u * t->w), y = (int)(v * t->h); if (x >= t->w) x = t->w - 1; if (y >= t->h) y = t->h - 1; return texel(t, x, y); }
    float fx = u * t->w - 0.5f, fy = v * t->h - 0.5f; int x0 = (int)floorf(fx), y0 = (int)floorf(fy); float ax = fx - x0, ay = fy - y0;
    int xs[2] = { x0, x0 + 1 }, ys[2] = { y0, y0 + 1 };
    for (int i = 0; i < 2; i++) { int m = s->stage[st].addr_u; if (m == 3 || m == 4) xs[i] = xs[i] < 0 ? 0 : xs[i] >= t->w ? t->w - 1 : xs[i]; else xs[i] = ((xs[i] % t->w) + t->w) % t->w;
        m = s->stage[st].addr_v; if (m == 3 || m == 4) ys[i] = ys[i] < 0 ? 0 : ys[i] >= t->h ? t->h - 1 : ys[i]; else ys[i] = ((ys[i] % t->h) + t->h) % t->h; }
    C4 a = texel(t, xs[0], ys[0]), b = texel(t, xs[1], ys[0]), c = texel(t, xs[0], ys[1]), d = texel(t, xs[1], ys[1]);
    #define L(f) ((a.f * (1 - ax) + b.f * ax) * (1 - ay) + (c.f * (1 - ax) + d.f * ax) * ay)
    return (C4){ L(r), L(g), L(b), L(a) }; }
typedef struct { C4 diff, spec, cur, tex, tf; } Ctx;
static C4 getarg(int arg, const Ctx *c) {
    C4 v; switch (arg & 15) { case 0: v = c->diff; break; case 1: v = c->cur; break; case 2: v = c->tex; break; case 3: v = c->tf; break; case 4: v = c->spec; break; default: v = c->cur; }
    if (arg & 0x20) v.r = v.g = v.b = v.a;                                     /* D3DTA_ALPHAREPLICATE */
    if (arg & 0x10) { v.r = 1 - v.r; v.g = 1 - v.g; v.b = 1 - v.b; v.a = 1 - v.a; }   /* D3DTA_COMPLEMENT */
    return v; }
static C4 combine(int op, C4 a, C4 b, const Ctx *c, int alpha_chan) {          /* returns rgba; caller keeps only rgb or only a */
    C4 r;
    #define PER(e) do { for (int k = 0; k < 4; k++) { float x = ((float *)&a)[k], y = ((float *)&b)[k]; ((float *)&r)[k] = (e); } } while (0)
    switch (op) {
    case 2: r = a; break; case 3: r = b; break;
    case 4: PER(x * y); break; case 5: PER(x * y * 2); break; case 6: PER(x * y * 4); break;
    case 7: PER(x + y); break; case 8: PER(x + y - 0.5f); break; case 9: PER((x + y - 0.5f) * 2); break;
    case 10: PER(x - y); break; case 11: PER(x + y - x * y); break;
    case 12: { float t = c->diff.a; PER(x * t + y * (1 - t)); break; }
    case 13: { float t = c->tex.a; PER(x * t + y * (1 - t)); break; }
    case 14: { float t = c->tf.a; PER(x * t + y * (1 - t)); break; }
    case 15: { float t = c->tex.a; PER(x + y * (1 - t)); break; }
    case 16: { float t = c->cur.a; PER(x * t + y * (1 - t)); break; }
    case 17: r = a; break;
    case 18: r = (C4){ a.r + a.a * b.r, a.g + a.a * b.g, a.b + a.a * b.b, a.a }; break;
    case 19: r = (C4){ a.r * b.r + a.a, a.g * b.g + a.a, a.b * b.b + a.a, a.a }; break;
    case 20: r = (C4){ (1 - a.a) * b.r + a.r, (1 - a.a) * b.g + a.g, (1 - a.a) * b.b + a.b, a.a }; break;
    case 21: r = (C4){ (1 - a.r) * b.r + a.a, (1 - a.g) * b.g + a.a, (1 - a.b) * b.b + a.a, a.a }; break;
    case 24: { float d = 4 * ((a.r - .5f) * (b.r - .5f) + (a.g - .5f) * (b.g - .5f) + (a.b - .5f) * (b.b - .5f)); r = (C4){ d, d, d, d }; break; }
    default: PER(x * y); }
    (void)alpha_chan; return r; }
static int cmp(int f, float a, float b) { switch (f) { case 1: return 0; case 2: return a < b; case 3: return a == b; case 4: return a <= b; case 5: return a > b; case 6: return a != b; case 7: return a >= b; default: return 1; } }
static float bfac(int f, C4 s, C4 d, int comp) {                                /* comp: 0=r 1=g 2=b 3=a */
    float sv = ((float *)&s)[comp], dv = ((float *)&d)[comp];
    switch (f) { case 1: return 0; case 2: return 1; case 3: return sv; case 4: return 1 - sv; case 5: return s.a; case 6: return 1 - s.a; case 7: return d.a; case 8: return 1 - d.a;
        case 9: return dv; case 10: return 1 - dv; case 11: { float m = s.a < 1 - d.a ? s.a : 1 - d.a; return comp == 3 ? 1 : m; } default: return 1; } }

static void tri(const GfxState *s, const GfxVertex *v0, const GfxVertex *v1, const GfxVertex *v2) {
    float area = (v1->x - v0->x) * (v2->y - v0->y) - (v2->x - v0->x) * (v1->y - v0->y);
    if (area == 0 || area != area) return;
    if (s->cull == 2 && area > 0) return;                                         /* D3DCULL_CW: clockwise on screen (y down) = positive area */
    if (s->cull == 3 && area < 0) return;
    if (area < 0) { const GfxVertex *t = v1; v1 = v2; v2 = t; area = -area; }
    int cx0, cy0, cx1, cy1; vp_rect(s, &cx0, &cy0, &cx1, &cy1);
    float minx = fminf(v0->x, fminf(v1->x, v2->x)), maxx = fmaxf(v0->x, fmaxf(v1->x, v2->x)), miny = fminf(v0->y, fminf(v1->y, v2->y)), maxy = fmaxf(v0->y, fmaxf(v1->y, v2->y));
    int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx), y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
    if (x0 < cx0) x0 = cx0; if (y0 < cy0) y0 = cy0; if (x1 > cx1) x1 = cx1; if (y1 > cy1) y1 = cy1;
    const GfxVertex *V[3] = { v0, v1, v2 }; float inv = 1.0f / area;
    C4 dc[3], sc[3]; for (int i = 0; i < 3; i++) { dc[i] = unpack(V[i]->diffuse); sc[i] = unpack(V[i]->specular); }
    C4 tfac = unpack(s->tfactor), fogc = unpack(s->fog_color);
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
        float px = x + 0.5f, py = y + 0.5f;
        float w0 = ((v2->x - v1->x) * (py - v1->y) - (v2->y - v1->y) * (px - v1->x)), w1 = ((v0->x - v2->x) * (py - v2->y) - (v0->y - v2->y) * (px - v2->x)), w2 = ((v1->x - v0->x) * (py - v0->y) - (v1->y - v0->y) * (px - v0->x));
        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
        float b0 = w0 * inv, b1 = w1 * inv, b2 = w2 * inv;
        float z = b0 * v0->z + b1 * v1->z + b2 * v2->z;
        if (z < 0 || z > 1) continue;
        if (s->z_enable && !cmp(s->z_func ? s->z_func : 4, z, zb[y * W + x])) continue;
        Ctx c; float bb[3] = { b0, b1, b2 };
        c.diff = (C4){ 0, 0, 0, 0 }; c.spec = c.diff; for (int i = 0; i < 3; i++) { c.diff.r += bb[i] * dc[i].r; c.diff.g += bb[i] * dc[i].g; c.diff.b += bb[i] * dc[i].b; c.diff.a += bb[i] * dc[i].a; c.spec.r += bb[i] * sc[i].r; c.spec.g += bb[i] * sc[i].g; c.spec.b += bb[i] * sc[i].b; c.spec.a += bb[i] * sc[i].a; }
        c.tf = tfac; c.cur = c.diff;
        float rh = b0 * v0->rhw + b1 * v1->rhw + b2 * v2->rhw;
        for (int st = 0; st < 3; st++) {
            int cop = s->stage[st].colorop; if (cop <= 1) break;
            const Tex *t = s->stage[st].tex > 0 && s->stage[st].tex < ntex_cap && texs[s->stage[st].tex].px ? &texs[s->stage[st].tex] : NULL;
            if (t) { int ci = s->stage[st].coordindex & 3; float u, vv;
                if (rh > 0) { u = (b0 * v0->u[ci] * v0->rhw + b1 * v1->u[ci] * v1->rhw + b2 * v2->u[ci] * v2->rhw) / rh; vv = (b0 * v0->v[ci] * v0->rhw + b1 * v1->v[ci] * v1->rhw + b2 * v2->v[ci] * v2->rhw) / rh; }
                else { u = b0 * v0->u[ci] + b1 * v1->u[ci] + b2 * v2->u[ci]; vv = b0 * v0->v[ci] + b1 * v1->v[ci] + b2 * v2->v[ci]; }
                c.tex = sample(t, s, st, u, vv); } else c.tex = (C4){ 1, 1, 1, 1 };
            C4 a1 = getarg(s->stage[st].colorarg1 ? s->stage[st].colorarg1 : 2, &c), a2 = getarg(s->stage[st].colorarg2 ? s->stage[st].colorarg2 : 1, &c);
            C4 rc = combine(cop, a1, a2, &c, 0), out = c.cur;
            out.r = rc.r; out.g = rc.g; out.b = rc.b;
            int aop = s->stage[st].alphaop;
            if (aop > 1) { C4 b1v = getarg(s->stage[st].alphaarg1 ? s->stage[st].alphaarg1 : 2, &c), b2v = getarg(s->stage[st].alphaarg2 ? s->stage[st].alphaarg2 : 1, &c); out.a = combine(aop, b1v, b2v, &c, 1).a; }
            c.cur = (C4){ sat(out.r), sat(out.g), sat(out.b), sat(out.a) }; }
        C4 o = c.cur;
        if (s->specular_enable) { o.r = sat(o.r + c.spec.r); o.g = sat(o.g + c.spec.g); o.b = sat(o.b + c.spec.b); }
        if (s->fog_enable) { float f = 1;                                          /* table fog on z; (vertex fog from specular alpha not distinguished: see handoff) */
            switch (s->fog_mode) { case 1: f = expf(-s->fog_density * z); break; case 2: f = expf(-(s->fog_density * z) * (s->fog_density * z)); break;
                default: f = (s->fog_end - s->fog_start) != 0 ? (s->fog_end - z) / (s->fog_end - s->fog_start) : 1; }
            f = sat(f); o.r = fogc.r + (o.r - fogc.r) * f; o.g = fogc.g + (o.g - fogc.g) * f; o.b = fogc.b + (o.b - fogc.b) * f; }
        if (s->alpha_test && !cmp(s->alpha_func ? s->alpha_func : 8, (float)(int)(o.a * 255.f + 0.5f), (float)(s->alpha_ref & 255))) continue;
        uint32_t *dp = &fb[y * W + x];
        if (s->blend_enable) { C4 d = unpack(*dp); d.a = 1;
            float fr = bfac(s->src_blend, o, d, 0), fg = bfac(s->src_blend, o, d, 1), fb_ = bfac(s->src_blend, o, d, 2);
            float dr = bfac(s->dst_blend, o, d, 0), dg = bfac(s->dst_blend, o, d, 1), db = bfac(s->dst_blend, o, d, 2);
            o.r = sat(o.r * fr + d.r * dr); o.g = sat(o.g * fg + d.g * dg); o.b = sat(o.b * fb_ + d.b * db); }
        *dp = 0xFF000000u | ((uint32_t)(sat(o.r) * 255.f + 0.5f) << 16) | ((uint32_t)(sat(o.g) * 255.f + 0.5f) << 8) | (uint32_t)(sat(o.b) * 255.f + 0.5f);
        if (s->z_write) zb[y * W + x] = z; } }
static void put_px(const GfxState *s, const GfxVertex *v) { int x = (int)v->x, y = (int)v->y; int a, b, c, d; vp_rect(s, &a, &b, &c, &d); if (x < a || y < b || x >= c || y >= d) return;
    if (s->z_enable && !cmp(s->z_func ? s->z_func : 4, v->z, zb[y * W + x])) return; fb[y * W + x] = v->diffuse | 0xFF000000u; if (s->z_write) zb[y * W + x] = v->z; }
static void line(const GfxState *s, const GfxVertex *a, const GfxVertex *b) {
    float dx = b->x - a->x, dy = b->y - a->y; int n = (int)fmaxf(fabsf(dx), fabsf(dy)); if (n < 1) n = 1;
    for (int i = 0; i <= n; i++) { GfxVertex p = *a; float t = (float)i / n; p.x += dx * t; p.y += dy * t; p.z += (b->z - a->z) * t; put_px(s, &p); } }
void gfx_draw(const GfxState *s, int prim, const GfxVertex *v, int nv, const uint16_t *idx, int ni) {
    if (!fb || !s) return;
    int n = idx ? ni : nv; if (n <= 0) return;
    #define V(i) (&v[idx ? idx[i] : (i)])
    switch (prim) {
    case GFX_POINTLIST: for (int i = 0; i < n; i++) put_px(s, V(i)); break;
    case GFX_LINELIST: for (int i = 0; i + 1 < n; i += 2) line(s, V(i), V(i + 1)); break;
    case GFX_LINESTRIP: for (int i = 0; i + 1 < n; i++) line(s, V(i), V(i + 1)); break;
    case GFX_TRIANGLELIST: for (int i = 0; i + 2 < n; i += 3) tri(s, V(i), V(i + 1), V(i + 2)); break;
    case GFX_TRIANGLESTRIP: for (int i = 0; i + 2 < n; i++) { if (i & 1) tri(s, V(i + 1), V(i), V(i + 2)); else tri(s, V(i), V(i + 1), V(i + 2)); } break;
    case GFX_TRIANGLEFAN: for (int i = 1; i + 1 < n; i++) tri(s, V(0), V(i), V(i + 1)); break;
    } }
void gfx_present(void) {
    static int every = -1; static const char *pre; if (every < 0) { pre = getenv("RT_GFX_DUMP"); const char *e = getenv("RT_GFX_DUMP_EVERY"); every = e ? atoi(e) : 1; if (every < 1) every = 1; }
    if (fb) gfx_soft_present(fb, W, H);
    if (pre && fb && frames % every == 0) { char nm[512]; snprintf(nm, sizeof nm, "%s%04d.ppm", pre, frames); FILE *f = fopen(nm, "wb");
        if (f) { fprintf(f, "P6\n%d %d\n255\n", W, H); for (int i = 0; i < W * H; i++) { uint32_t c = fb[i]; fputc((c >> 16) & 255, f); fputc((c >> 8) & 255, f); fputc(c & 255, f); } fclose(f); } }
    frames++; }
