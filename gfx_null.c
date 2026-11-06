/* gfx_null.c - logging backend for the PC test rig: counts what the renderer asks for, draws nothing. RT_GFX_TRACE=1 prints each call. */
#include "gfx.h"
#include <stdio.h>
#include <stdlib.h>
static int trace, frames; static unsigned long ndraw, ntri, nclear, ntex, nup; static int rt_w, rt_h;
__attribute__((weak)) int gfx_init(int w, int h) { trace = getenv("RT_GFX_TRACE") != NULL; rt_w = w; rt_h = h; fprintf(stderr, "[gfx-null] init %dx%d\n", w, h); return 0; }
__attribute__((weak)) void gfx_set_mode(int w, int h) { rt_w = w; rt_h = h; fprintf(stderr, "[gfx-null] mode %dx%d\n", w, h); }
__attribute__((weak)) uint32_t gfx_tex_create(int w, int h) { static uint32_t n; ntex++; if (trace) fprintf(stderr, "[gfx-null] tex_create %dx%d -> %u\n", w, h, n + 1); return ++n; }
__attribute__((weak)) void gfx_tex_upload(uint32_t t, const uint8_t *p, int w, int h) { nup++; if (trace) fprintf(stderr, "[gfx-null] tex_upload %u %dx%d first px %02x%02x%02x%02x\n", t, w, h, p[0], p[1], p[2], p[3]); }
__attribute__((weak)) void gfx_tex_delete(uint32_t t) { if (trace) fprintf(stderr, "[gfx-null] tex_delete %u\n", t); }
__attribute__((weak)) void gfx_begin_scene(void) {}
__attribute__((weak)) void gfx_end_scene(void) {}
__attribute__((weak)) void gfx_clear(int f, uint32_t c, float z, const GfxState *s) { nclear++; if (trace) fprintf(stderr, "[gfx-null] clear flags=%d color=%08x z=%g\n", f, c, z); }
__attribute__((weak)) void gfx_draw(const GfxState *s, int prim, const GfxVertex *v, int nv, const uint16_t *idx, int ni) {
    ndraw++; ntri += (unsigned long)((idx ? ni : nv) / (prim == GFX_TRIANGLELIST ? 3 : 1));
    if (trace) fprintf(stderr, "[gfx-null] draw prim=%d nv=%d ni=%d tex0=%d tex1=%d blend=%d(%d,%d) z=%d/%d v0=(%.1f,%.1f,%.3f w=%.4f c=%08x uv=%.2f,%.2f)\n", prim, nv, ni, s->stage[0].tex, s->stage[1].tex, s->blend_enable, s->src_blend, s->dst_blend, s->z_enable, s->z_write, v[0].x, v[0].y, v[0].z, v[0].rhw, v[0].diffuse, v[0].u[0], v[0].v[0]);
}
__attribute__((weak)) void gfx_present(void) { frames++; if (frames % 60 == 1 || trace) fprintf(stderr, "[gfx-null] present #%d (draws=%lu tris~%lu clears=%lu tex=%lu uploads=%lu)\n", frames, ndraw, ntri, nclear, ntex, nup); }
