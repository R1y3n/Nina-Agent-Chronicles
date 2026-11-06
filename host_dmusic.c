/* host_dmusic.c - DirectMusic stub: lets the game's music manager initialise and run silently.
 * The game creates a Performance + Loader and plays style/segment (.sty/.sgt) files; faithfully rendering those needs a DLS synthesizer + style
 * sequencer, which is out of scope.  These objects accept every call, return "nothing found" from enumerations (S_FALSE, so loops end) and hand out
 * further stub objects through out-parameters.  Replacing the Segment object's behaviour (e.g. mapping a segment name to a streamed audio file) is the
 * hook for real music later.
 *
 * IMPORTANT (stdcall): COM methods are callee-pops, so EVERY vtable slot must be minted with the exact dword count of the real method
 * (this + args; 64-bit REFERENCE_TIME / MUSIC_TIME-pair args count 2).  A wrong count unbalances ESP and corrupts the caller's restored registers.
 * The tables below hold those counts per interface; slots beyond a table default to 2 (this + 1) and are logged under RT_DM_TRACE when called. */
#include "rt.h"
#define API(n) static uint32_t api_##n(const uint32_t *a)
#define D(n, k) { #n, k, api_##n }
static int dm_trace(void) { static int t = -1; if (t < 0) t = getenv("RT_DM_TRACE") != NULL; return t; }
#define TR(...) do { if (dm_trace()) rt_log("[dmusic] " __VA_ARGS__); } while (0)
enum { K_PERF = 1, K_LOADER, K_DM, K_PORT, K_SEG, K_STATE, K_OTHER, K_COUNT };
#define NSLOT 64
static uint32_t vt[K_COUNT];
static uint32_t mk(int kind) { uint32_t o = rt_guest_alloc(16); WR32(o, vt[kind]); WR32(o + 4, 1); WR32(o + 8, (uint32_t)kind); return o; }
static void put(uint32_t p, uint32_t v) { if (p) WR32(p, v); }
API(qi) { if (a[2]) { WR32(a[2], a[0]); WR32(a[0] + 4, RD32(a[0] + 4) + 1); } return 0; }
API(addref) { uint32_t r = RD32(a[0] + 4) + 1; WR32(a[0] + 4, r); return r; }
API(release) { uint32_t r = RD32(a[0] + 4); if (r) r--; WR32(a[0] + 4, r); return r; }
/* generic "succeed" / "S_FALSE" stubs, one per dword count */
#define GEN(n) API(ok##n) { (void)a; TR("slot (%d args) -> S_OK", n); return 0; } API(sf##n) { (void)a; TR("slot (%d args) -> S_FALSE", n); return 1; }
GEN(1) GEN(2) GEN(3) GEN(4) GEN(5) GEN(6) GEN(7) GEN(8) GEN(9) GEN(10)
static const ApiFn OKF[11] = { 0, api_ok1, api_ok2, api_ok3, api_ok4, api_ok5, api_ok6, api_ok7, api_ok8, api_ok9, api_ok10 };
static const ApiFn SFF[11] = { 0, api_sf1, api_sf2, api_sf3, api_sf4, api_sf5, api_sf6, api_sf7, api_sf8, api_sf9, api_sf10 };
/* ---- methods with real behaviour (args include `this` as a[0]) ---- */
API(perf_Init) { TR("Performance::Init"); if (a[1] && !RD32(a[1])) WR32(a[1], mk(K_DM)); return 0; }                    /* (this, IDirectMusic**, IDirectSound*, HWND) */
API(perf_PlaySegment) { TR("Performance::PlaySegment"); put(a[5], mk(K_STATE)); return 0; }                            /* (this, seg, flags, time lo, time hi, IDirectMusicSegmentState**) */
API(perf_GetSegmentState) { put(a[2], mk(K_STATE)); return 0; }                                                       /* (this, MUSIC_TIME, IDirectMusicSegmentState**) */
API(perf_GetTime) { if (a[1]) { WR32(a[1], 0); WR32(a[1] + 4, 0); } put(a[2], 0); return 0; }                          /* (this, REFERENCE_TIME*, MUSIC_TIME*) */
API(perf_AllocPMsg) { put(a[2], rt_guest_alloc(a[1] + 16)); return 0; }                                               /* (this, cb, DMUS_PMSG**) */
API(dm_CreatePort) { put(a[3], mk(K_PORT)); return 0; }                                                               /* (this, rclsid, params, IDirectMusicPort**, outer) */
API(loader_GetObject) { TR("Loader::GetObject"); put(a[3], mk(K_SEG)); return 0; }                                    /* (this, DMUS_OBJECTDESC*, riid, void**) */
API(seg_InitPlay) { put(a[1], mk(K_STATE)); return 0; }                                                               /* (this, IDirectMusicSegmentState**, IDirectMusicPerformance*, flags) */
API(seg_Clone) { put(a[3], mk(K_SEG)); return 0; }                                                                    /* (this, mtStart, mtEnd, IDirectMusicSegment**) */
/* ---- per-interface dword counts (slot 3 first) ---- */
static const uint8_t NA_PERF[] = { 4,6,6,3,2,2,2,2,2,3,4,3,3,3,2,2,2,4,2,2,2,2,2,4,5,5,9,3,7,6,4,4,2,2,3,1,5,5,6,7,7 };     /* IDirectMusicPerformance: Init..RhythmToTime (slots 3..43) */
static const uint8_t NA_DM[]   = { 3,4,5,3,3,2,2,2,3 };                                                                  /* IDirectMusic: EnumPort..SetDirectSound */
static const uint8_t NA_LOAD[] = { 4,2,4,4,2,2,2,3,4 };                                                                  /* IDirectMusicLoader: GetObject..EnumObject (slots 3..11) */
static const uint8_t NA_PORT[] = { 2,2,2,5,2,2,2,1,2,7,2,2,2,4,4,3,4 };                                                  /* IDirectMusicPort: PlayBuffer..GetFormat */
static const uint8_t NA_SEG[]  = { 2,2,2,2,2,2,5,4,3,2,4,2,2,2,2,7,6,4,2,2,3,3,3,6,2,5,2,2 };                            /* IDirectMusicSegment(8): GetLength..Unload */
static const uint8_t NA_STATE[]= { 2,2,2,2,2 };                                                                          /* IDirectMusicSegmentState */
typedef struct { const uint8_t *na; int n; } Tab;
static const Tab TABS[K_COUNT] = { {0,0}, {NA_PERF,sizeof NA_PERF}, {NA_LOAD,sizeof NA_LOAD}, {NA_DM,sizeof NA_DM}, {NA_PORT,sizeof NA_PORT}, {NA_SEG,sizeof NA_SEG}, {NA_STATE,sizeof NA_STATE}, {0,0} };
static void set_custom(int kind, int slot, int nargs, const char *name, ApiFn fn) { WR32(vt[kind] + 4 * slot, rt_host_func(name, nargs, fn)); }
static void build(void) {
    if (vt[1]) return;
    uint32_t q = rt_host_func("DM::QueryInterface", 3, api_qi), ar = rt_host_func("DM::AddRef", 1, api_addref), rl = rt_host_func("DM::Release", 1, api_release);
    uint32_t okm[11] = {0}, sfm[11] = {0};
    for (int n = 1; n <= 10; n++) { okm[n] = rt_host_func("DM::ok", n, OKF[n]); sfm[n] = rt_host_func("DM::S_FALSE", n, SFF[n]); }
    for (int k = 1; k < K_COUNT; k++) {
        vt[k] = rt_guest_alloc(4 * NSLOT); WR32(vt[k], q); WR32(vt[k] + 4, ar); WR32(vt[k] + 8, rl);
        for (int i = 3; i < NSLOT; i++) { int n = (i - 3 < TABS[k].n) ? TABS[k].na[i - 3] : 2; WR32(vt[k] + 4 * i, okm[n]); }
    }
    /* IDirectMusicPerformance */
    set_custom(K_PERF, 3, 4, "Performance::Init", api_perf_Init);                 set_custom(K_PERF, 4, 6, "Performance::PlaySegment", api_perf_PlaySegment);
    set_custom(K_PERF, 6, 3, "Performance::GetSegmentState", api_perf_GetSegmentState);
    WR32(vt[K_PERF] + 4 * 14, sfm[3]);                                            /* IsPlaying -> S_FALSE */
    set_custom(K_PERF, 15, 3, "Performance::GetTime", api_perf_GetTime);          set_custom(K_PERF, 16, 3, "Performance::AllocPMsg", api_perf_AllocPMsg);
    /* IDirectMusic */
    WR32(vt[K_DM] + 4 * 3, sfm[3]); WR32(vt[K_DM] + 4 * 6, sfm[3]);               /* EnumPort / EnumMasterClock -> S_FALSE (end of list) */
    set_custom(K_DM, 5, 5, "DM::CreatePort", api_dm_CreatePort);
    /* IDirectMusicLoader */
    set_custom(K_LOADER, 3, 4, "Loader::GetObject", api_loader_GetObject);        WR32(vt[K_LOADER] + 4 * 11, sfm[4]);   /* EnumObject -> S_FALSE */
    /* IDirectMusicSegment */
    set_custom(K_SEG, 13, 4, "Segment::InitPlay", api_seg_InitPlay);              set_custom(K_SEG, 20, 4, "Segment::Clone", api_seg_Clone);
}
API(CoCreateInstance) {                                                                  /* (rclsid, outer, clsctx, riid, ppv) */
    uint32_t c = RD32(a[0]); int kind = c == 0xD2AC2881u ? K_PERF : c == 0xD2AC2892u ? K_LOADER : c == 0xD2AC2890u ? K_OTHER : c == 0x636B9F8Cu ? K_DM : 0;   /* Performance / Loader / Composer / DirectMusic */
    TR("CoCreateInstance(clsid=%08x) -> %s", c, kind ? "stub object" : "REGDB_E_CLASSNOTREG");
    if (!kind) { if (a[4]) WR32(a[4], 0); return 0x80040154u; }
    build(); WR32(a[4], mk(kind)); return 0;
}
const ApiDef rt_host_dmusic_defs[] = { D(CoCreateInstance,5), { 0, 0, 0 } };
