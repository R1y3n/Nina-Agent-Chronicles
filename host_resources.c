/* host_resources.c - PE resource access for the recompiled modules: FindResource/LoadResource/LockResource/SizeofResource,
 * LoadStringA and FormatMessageA (message tables).  The resource directory is parsed directly from the module image that
 * the loader already placed in guest memory, so it works for every lifted DLL/EXE. */
#include "rt.h"
#include <ctype.h>
#define GSTR(p) ((const char *)(G + (p)))
#define API(n) static uint32_t api_##n(const uint32_t *a)
#define D(n, k) { #n, k, api_##n }

static uint32_t res_root(uint32_t base, uint32_t *size) {      /* guest address of the resource directory, 0 if none */
    if (RD16(base) != 0x5A4D) return 0;
    uint32_t pe = base + RD32(base + 0x3C); if (RD32(pe) != 0x4550) return 0;
    uint32_t rva = RD32(pe + 0x88); if (size) *size = RD32(pe + 0x8C);
    return rva ? base + rva : 0;
}
static int name_eq(uint32_t root, uint32_t entry_name, uint32_t want) {        /* want: MAKEINTRESOURCE id or guest string pointer */
    if (want < 0x10000) return !(entry_name & 0x80000000u) && entry_name == want;
    if (!(entry_name & 0x80000000u)) return 0;
    uint32_t s = root + (entry_name & 0x7FFFFFFFu); uint16_t n = RD16(s); const char *w = GSTR(want);
    if (strlen(w) != n) return 0;
    for (int i = 0; i < n; i++) if (toupper((unsigned char)w[i]) != toupper(RD16(s + 2 + 2 * i))) return 0;
    return 1;
}
/* returns guest address of the IMAGE_RESOURCE_DATA_ENTRY or 0 */
static uint32_t find_res(uint32_t base, uint32_t type, uint32_t name) {
    uint32_t root = res_root(base, NULL); if (!root) return 0;
    uint32_t want[2] = { type, name }, dir = root;
    for (int level = 0; level < 3; level++) {
        uint16_t nn = RD16(dir + 12), ni = RD16(dir + 14); uint32_t e = dir + 16, found = 0;
        for (int i = 0; i < nn + ni; i++, e += 8) {
            uint32_t nm = RD32(e), off = RD32(e + 4);
            if (level < 2 ? name_eq(root, nm, want[level]) : 1) { found = off; break; }     /* level 2 = language: take the first */
        }
        if (!found) return 0;
        if (level == 2) return found & 0x80000000u ? 0 : root + found;
        if (!(found & 0x80000000u)) return 0;
        dir = root + (found & 0x7FFFFFFFu);
    }
    return 0;
}
static uint32_t hmod_base(uint32_t h) { return h ? h : (rt_exe ? rt_exe->base : 0); }

API(FindResourceA) { uint32_t e = find_res(hmod_base(a[0]), a[2], a[1]); if (!e) rt_set_last_error(1814); return e; }
API(LoadResource) { uint32_t e = a[1]; return e ? hmod_base(a[0]) + RD32(e) : 0; }              /* HGLOBAL = guest address of the data */
API(LockResource) { return a[0]; }
API(SizeofResource) { return a[1] ? RD32(a[1] + 4) : 0; }

API(LoadStringA) {
    uint32_t id = a[1], block = id / 16 + 1; uint32_t e = find_res(hmod_base(a[0]), 6, block);
    if (!e) return 0;
    uint32_t p = hmod_base(a[0]) + RD32(e), end = p + RD32(e + 4);
    for (uint32_t i = 0; i < id % 16; i++) { if (p >= end) return 0; p += 2 + 2 * RD16(p); }
    uint16_t n = RD16(p); if (p >= end) return 0;
    if (!a[3]) { return 0; }
    uint32_t cnt = n < a[3] - 1 ? n : a[3] - 1;
    for (uint32_t i = 0; i < cnt; i++) { uint16_t c = RD16(p + 2 + 2 * i); G[a[2] + i] = c < 0x80 ? (uint8_t)c : '?'; }
    G[a[2] + cnt] = 0; return cnt;
}

/* FormatMessageA: FROM_HMODULE message tables + minimal FROM_SYSTEM text; %1..%99 inserts from an argument array */
API(FormatMessageA) {
    uint32_t flags = a[0], msgid = a[2], args = a[6]; char text[2048]; int have = 0;
    if (flags & 0x800) {
        uint32_t base = hmod_base(a[1]); uint32_t e = find_res(base, 11, 1);
        if (e) { uint32_t d = base + RD32(e), nb = RD32(d);
            for (uint32_t b = 0; b < nb && !have; b++) {
                uint32_t lo = RD32(d + 4 + 12 * b), hi = RD32(d + 8 + 12 * b), off = RD32(d + 12 + 12 * b);
                if (msgid < lo || msgid > hi) continue;
                uint32_t p = d + off; for (uint32_t i = lo; i < msgid; i++) p += RD16(p);
                uint16_t len = RD16(p), fl = RD16(p + 2); uint32_t k = 0;
                if (fl & 1) { for (uint32_t i = 4; i + 1 < len && k < sizeof text - 1; i += 2) { uint16_t c = RD16(p + i); if (!c) break; text[k++] = c < 0x80 ? (char)c : '?'; } }
                else { for (uint32_t i = 4; i < len && k < sizeof text - 1 && G[p + i]; i++) text[k++] = (char)G[p + i]; }
                text[k] = 0; have = 1;
            } }
    }
    if (!have && (flags & 0x1000)) { snprintf(text, sizeof text, "Windows error %u\r\n", msgid); have = 1; }
    if (!have) { rt_set_last_error(317); return 0; }               /* ERROR_MR_MID_NOT_FOUND */
    char out[2400]; uint32_t o = 0;
    for (const char *s = text; *s && o < sizeof out - 40; s++) {
        if (*s != '%' || (flags & 0x200)) { out[o++] = *s; continue; }
        s++;
        if (*s == '%') out[o++] = '%'; else if (*s == 'n') { out[o++] = '\r'; out[o++] = '\n'; } else if (*s == '0') break;
        else if (isdigit((unsigned char)*s)) {
            int n = 0; while (isdigit((unsigned char)*s)) n = n * 10 + (*s++ - '0'); char conv = 's';
            if (*s == '!') { s++; char f[16]; int k = 0; while (*s && *s != '!' && k < 15) f[k++] = *s++; f[k] = 0; conv = k ? f[k - 1] : 's'; } else s--;
            if (args && (flags & 0x2000)) { uint32_t v = RD32(args + 4 * (n - 1)); o += conv == 'd' || conv == 'u' ? (uint32_t)snprintf(out + o, 40, "%d", (int)v) : conv == 'x' ? (uint32_t)snprintf(out + o, 40, "%x", v) : (uint32_t)snprintf(out + o, 200, "%s", v ? GSTR(v) : "(null)"); }
            else o += (uint32_t)snprintf(out + o, 12, "<%d>", n);
        } else out[o++] = '%';
    }
    out[o] = 0;
    if (flags & 0x100) { uint32_t p = rt_guest_alloc(o + 1); memcpy(G + p, out, o + 1); WR32(a[4], p); return o; }     /* ALLOCATE_BUFFER */
    if (o + 1 > a[5]) { rt_set_last_error(122); return 0; }
    memcpy(G + a[4], out, o + 1); return o;
}

const ApiDef rt_host_res_defs[] = {
 D(FindResourceA,3),D(LoadResource,2),D(LockResource,1),D(SizeofResource,2),D(LoadStringA,4),D(FormatMessageA,7), { 0, 0, 0 }
};
