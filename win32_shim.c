/* Host implementation of the Win32 APIs imported by the recompiled modules (stdcall).
 * Bootstrap-grade: enough to run CRT startup, heap, strings, time, TLS, module loading.  Everything the shim does
 * not implement is generated into api_stubs_generated.c (traps with the API name when called) -- see api_manifest.md.
 * On a console port replace/extend these with calls into your platform layer. */
#include "rt.h"
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>

static uint32_t last_error;
static uint32_t heap_top = GUEST_HEAP_BASE + 0x10000, va_top = GUEST_VA_BASE, scratch_top = GUEST_SCRATCH;
static int trace_api;
static uint32_t tls_slots[64]; static int tls_used[64];
static int deterministic(void) { static int d = -1; if (d < 0) d = getenv("RT_DETERMINISTIC") != NULL; return d; }   /* fixed-step clocks for lockstep verification */

#define API(n) static uint32_t api_##n(const uint32_t *a)
#define GSTR(p) ((const char *)(G + (p)))

static uint32_t scratch_alloc(uint32_t n) { uint32_t r = scratch_top; scratch_top += (n + 15) & ~15u; return r; }
static uint32_t guest_heap_alloc(uint32_t n) {
    uint32_t sz = (n + 15) & ~15u, p = heap_top + 16;   /* 16-byte header holds the requested size */
    if ((uint64_t)heap_top + 16 + sz > GUEST_HEAP_END) { last_error = 8; return 0; }
    WR32(heap_top, n); heap_top += 16 + sz; return p;
}
void rt_set_last_error(uint32_t e) { last_error = e; }
uint32_t rt_scratch_alloc(uint32_t n) { return scratch_alloc(n); }
uint32_t rt_guest_alloc(uint32_t n) { return guest_heap_alloc(n); }
__attribute__((weak)) const ApiDef rt_host_defs[] = { {0, 0, 0} };
__attribute__((weak)) const ApiDef rt_host_user32_defs[] = { {0, 0, 0} };
__attribute__((weak)) const ApiDef rt_host_res_defs[] = { {0, 0, 0} };
__attribute__((weak)) const ApiDef rt_host_threads_defs[] = { {0, 0, 0} };
__attribute__((weak)) const ApiDef rt_host_dinput_defs[] = { {0, 0, 0} };
__attribute__((weak)) const ApiDef rt_host_gdi_defs[] = { {0, 0, 0} };
__attribute__((weak)) const ApiDef rt_host_ddraw_defs[] = { {0, 0, 0} };
__attribute__((weak)) const ApiDef rt_host_dmusic_defs[] = { {0, 0, 0} };
__attribute__((weak)) const ApiDef rt_host_audio_defs[] = { {0, 0, 0} };
__attribute__((weak)) void host_audio_pump(void) {}
static const char *exe_name(void) { return rt_exe ? rt_exe->file : "app.exe"; }

/* ---- memory (bump allocator: HeapFree does not recycle; replace with your allocator for long runs) ---- */
API(HeapCreate)  { return GUEST_HEAP_BASE; }
API(HeapDestroy) { return 1; }
API(GetProcessHeap) { return GUEST_HEAP_BASE; }
API(HeapAlloc)   { return guest_heap_alloc(a[2]); }
API(HeapFree)    { return 1; }
API(HeapSize)    { return a[2] ? RD32(a[2] - 16) : (uint32_t)-1; }
API(HeapValidate){ return 1; }
API(HeapReAlloc) { uint32_t old = a[2], n = a[3], p = guest_heap_alloc(n), os = RD32(old - 16);
                   if (p) memcpy(G + p, G + old, os < n ? os : n); return p; }
API(VirtualAlloc){ uint32_t sz = (a[1] + 0xFFFF) & ~0xFFFFu, p;
                   if (a[0]) return a[0];
                   if ((uint64_t)va_top + sz > GUEST_VA_END) { last_error = 8; return 0; }
                   p = va_top; va_top += sz; return p; }
API(VirtualFree) { return 1; }
API(GlobalAlloc) { return guest_heap_alloc(a[1]); }
API(GlobalFree)  { return 0; }
API(GlobalLock)  { return a[0]; }
API(GlobalUnlock){ return 1; }
API(GlobalHandle){ return a[0]; }
API(LocalFree)   { return 0; }
API(IsBadReadPtr){ return 0; }
API(IsBadWritePtr){ return 0; }
API(IsBadCodePtr){ return 0; }

/* ---- process / module ---- */
API(GetVersion)  { return 0x0A280105u; }               /* Windows XP 5.1.2600 */
API(GetVersionExA){ uint32_t p = a[0]; memset(G + p + 4, 0, RD32(p) - 4);
                    WR32(p + 4, 5); WR32(p + 8, 1); WR32(p + 12, 2600); WR32(p + 16, 2); return 1; }
static uint32_t cmdline_guest;
void rt_set_command_line(const char *s) { cmdline_guest = scratch_alloc((uint32_t)strlen(s) + 1); strcpy((char *)G + cmdline_guest, s); }
API(GetCommandLineA){ if (!cmdline_guest) { char b[300]; snprintf(b, sizeof b, "\"C:\\Game\\%s\"", exe_name()); rt_set_command_line(b); } return cmdline_guest; }
API(GetModuleHandleA){ if (!a[0]) return rt_exe ? rt_exe->base : 0x400000; const ModuleInfo *m = rt_find_module(GSTR(a[0])); return m ? m->base : 0; }
API(GetModuleFileNameA){ char b[300]; const ModuleInfo *m = a[0] ? rt_module_from_base(a[0]) : rt_exe;
                    snprintf(b, sizeof b, "C:\\Game\\%s", m ? m->file : exe_name()); uint32_t n = (uint32_t)strlen(b); if (n >= a[2]) n = a[2] - 1;
                    memcpy(G + a[1], b, n); G[a[1] + n] = 0; return n; }
API(LoadLibraryA){ const ModuleInfo *m = rt_find_module(GSTR(a[0]));
                   if (!m) { last_error = 126; if (trace_api) fprintf(stderr, "  [shim] LoadLibrary(%s) -> not recompiled\n", GSTR(a[0])); return 0; }
                   rt_call_dllmain(m, 1); return m->base; }
API(FreeLibrary){ return 1; }
API(GetProcAddress){ const ModuleInfo *m = rt_module_from_base(a[0]); uint32_t r;
                   if (!m) { last_error = 126; return 0; }
                   r = a[1] < 0x10000 ? rt_find_export(m, 0, (uint16_t)a[1]) : rt_find_export(m, GSTR(a[1]), 0);
                   if (!r) last_error = 127; return r; }
API(GetStartupInfoA){ memset(G + a[0], 0, 68); WR32(a[0], 68); return 0; }
API(GetCurrentProcess){ return 0xFFFFFFFFu; }
API(GetCurrentThread){ return 0xFFFFFFFEu; }
API(GetCurrentProcessId){ return 1000; }
API(GetCurrentThreadId){ return 1001; }
static void dump_state(void) {
    const char *p = getenv("RT_DUMP"), *r = getenv("RT_DUMP_DATA"); if (!p) return; FILE *f = fopen(p, "wb");
    unsigned va = 0, n = 0; if (r) sscanf(r, "%x:%x", &va, &n);           /* "VA:size" of the .data/.bss range to include */
    if (n) fwrite(G + va, 1, n, f);
    fwrite(G + GUEST_HEAP_BASE, 1, heap_top - GUEST_HEAP_BASE, f);
    fwrite(G + GUEST_SCRATCH, 1, scratch_top - GUEST_SCRATCH, f);
    if (ESP < GUEST_STACK_TOP && ESP > GUEST_STACK_TOP - GUEST_STACK_SIZE) fwrite(G + ESP, 1, GUEST_STACK_TOP - ESP, f);
    fclose(f);
}
API(ExitProcess) {
    if (trace_api) fprintf(stderr, "API ExitProcess(%x) = 0\n", a[0]);
    dump_state();
    fflush(stdout);
#ifndef RT_ORBIS
    if (getenv("NINA_KEEP_GUI")) {
        host_gui_message("Nina", "The engine requested ExitProcess; diagnostic GUI remains open.");
        for (;;) { host_gui_pump(); usleep(16000); }
    }
#endif
    if (rt_exit_hook) rt_exit_hook((int)a[0]);
    exit((int)a[0]);
}
API(TerminateProcess){ return api_ExitProcess(a + 1); }
API(UnhandledExceptionFilter){ return 0; }
API(SetUnhandledExceptionFilter){ return 0; }
API(RtlUnwind)   { rt_trap(0, "RtlUnwind called (SEH unwinding is not emulated)"); }
API(GetLastError){ return last_error; }
API(SetLastError){ last_error = a[0]; return 0; }
API(SetErrorMode){ return 0; }
API(SetHandleCount){ return a[0]; }
API(CloseHandle) { return 1; }
API(CreateMutexA){ last_error = 0; return 0x100; }
API(ReleaseMutex){ return 1; }
API(WaitForSingleObject){ return 0; }
API(GetExitCodeProcess){ WR32(a[1], 0); return 1; }
API(CreateProcessA){
    fprintf(stderr, "[shim] CreateProcessA app=%s cmd=%s\n", a[0] ? GSTR(a[0]) : "(null)", a[1] ? GSTR(a[1]) : "(null)");
    uint32_t pi = a[9]; WR32(pi, 0x200); WR32(pi + 4, 0x201); WR32(pi + 8, 1234); WR32(pi + 12, 1235); return 1; }
API(OutputDebugStringA){ if (trace_api) fprintf(stderr, "[debug] %s\n", GSTR(a[0])); return 0; }

/* ---- directories (fixed fake Windows layout) ---- */
static uint32_t put_str(uint32_t buf, uint32_t size, const char *s) { uint32_t n = (uint32_t)strlen(s); if (n + 1 > size) return n + 1; memcpy(G + buf, s, n + 1); return n; }
API(GetWindowsDirectoryA){ return put_str(a[0], a[1], "C:\\WINDOWS"); }
API(GetSystemDirectoryA) { return put_str(a[0], a[1], "C:\\WINDOWS\\SYSTEM32"); }
API(GetCurrentDirectoryA){ return put_str(a[1], a[0], "C:\\Game"); }

API(GetDiskFreeSpaceA){ WR32(a[1], 8); WR32(a[2], 512); WR32(a[3], 1000000); WR32(a[4], 2000000); return 1; }   /* fake: 8 sectors/cluster, 512 B/sector */

API(GetSystemTimeAsFileTime){ uint64_t ft = 0x01C5A8D012345678ull;      /* 100ns ticks since 1601 */
    if (!deterministic()) { struct timespec t; clock_gettime(CLOCK_REALTIME, &t); ft = ((uint64_t)t.tv_sec * 10000000ull) + t.tv_nsec / 100 + 116444736000000000ull; }
    WR64(a[0], ft); return 0; }

API(CoInitialize){ return 0; }       /* S_OK: no COM runtime here; real COM objects must be provided by the host layer */
API(CoUninitialize){ return 0; }

/* ---- time ---- */
API(GetTickCount){ static uint32_t det; if (deterministic()) return det += 15; struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint32_t)(t.tv_sec * 1000u + t.tv_nsec / 1000000u); }
API(timeGetTime){ host_audio_pump(); return api_GetTickCount(a); }
API(QueryPerformanceFrequency){ WR32(a[0], 1000000000u); WR32(a[0] + 4, 0); return 1; }
API(QueryPerformanceCounter){ static uint64_t det; WR64(a[0], deterministic() ? (det += 10000000) : rt_rdtsc()); return 1; }
API(Sleep){ if (!deterministic()) usleep(a[0] * 1000u); return 0; }

/* ---- sync (single-threaded model) / TLS ---- */
API(InitializeCriticalSection){ return 0; }
API(DeleteCriticalSection){ return 0; }
API(EnterCriticalSection){ return 0; }
API(LeaveCriticalSection){ return 0; }
API(InterlockedIncrement){ uint32_t v = RD32(a[0]) + 1; WR32(a[0], v); return v; }
API(InterlockedDecrement){ uint32_t v = RD32(a[0]) - 1; WR32(a[0], v); return v; }
API(InterlockedExchange){ uint32_t o = RD32(a[0]); WR32(a[0], a[1]); return o; }
API(TlsAlloc)    { for (int i = 0; i < 64; i++) if (!tls_used[i]) { tls_used[i] = 1; tls_slots[i] = 0; return i; } return 0xFFFFFFFFu; }
API(TlsFree)     { if (a[0] < 64) tls_used[a[0]] = 0; return 1; }
API(TlsGetValue) { return a[0] < 64 ? tls_slots[a[0]] : 0; }
API(TlsSetValue) { if (a[0] < 64) tls_slots[a[0]] = a[1]; return 1; }

/* ---- strings ---- */
API(lstrlenA)    { return (uint32_t)strlen(GSTR(a[0])); }
API(lstrcpyA)    { strcpy((char *)G + a[0], GSTR(a[1])); return a[0]; }
API(lstrcatA)    { strcat((char *)G + a[0], GSTR(a[1])); return a[0]; }

/* ---- environment ---- */
API(GetEnvironmentStrings){ static uint32_t p; if (!p) { p = scratch_alloc(64); memcpy(G + p, "SystemRoot=C:\\WINDOWS\0\0", 24);} return p; }
API(GetEnvironmentStringsW){ static uint32_t p; if (!p) { const char *s = "SystemRoot=C:\\WINDOWS"; p = scratch_alloc(128);
                    uint32_t n = 0; for (; s[n]; n++) WR16(p + 2*n, (uint16_t)s[n]); } return p; }
API(FreeEnvironmentStringsA){ return 1; }
API(FreeEnvironmentStringsW){ return 1; }
API(GetEnvironmentVariableA){ const char *v = getenv(GSTR(a[0])); if (!v) { last_error = 203; return 0; }
                    uint32_t n = (uint32_t)strlen(v); if (n + 1 > a[2]) return n + 1; memcpy(G + a[1], v, n + 1); return n; }

/* ---- files / console ---- */
API(GetStdHandle){ return a[0] == (uint32_t)-10 ? 3 : a[0] == (uint32_t)-11 ? 4 : 5; }
API(GetFileType) { return 2; }   /* FILE_TYPE_CHAR */
API(WriteFile)   { int fd = a[0] == 4 ? 1 : a[0] == 5 ? 2 : -1; if (fd < 0) return 0;
                   ssize_t n = write(fd, G + a[1], a[2]); if (a[3]) WR32(a[3], n < 0 ? 0 : (uint32_t)n); return n >= 0; }
API(GetFileAttributesA){ char p[512]; strncpy(p, GSTR(a[0]), 511); p[511] = 0;
                   for (char *c = p; *c; c++) if (*c == '\\') *c = '/';
                   struct stat st; if (stat(p, &st) != 0) { last_error = 2; return 0xFFFFFFFFu; } return S_ISDIR(st.st_mode) ? 0x10 : 0x20; }

/* ---- code pages / strings (ASCII-only model; sufficient for CRT init) ---- */
API(GetACP)   { return 1252; }
API(GetOEMCP) { return 437; }
API(GetCPInfo){ memset(G + a[1], 0, 20); WR32(a[1], 1); G[a[1] + 4] = '?'; return 1; }
API(MultiByteToWideChar){ int32_t n = (int32_t)a[3]; const uint8_t *s = G + a[2]; if (n < 0) n = (int32_t)strlen((const char *)s) + 1;
                   if (a[5] == 0) return (uint32_t)n; if ((uint32_t)n > a[5]) { last_error = 122; return 0; }
                   for (int i = 0; i < n; i++) WR16(a[4] + 2*i, s[i]); return (uint32_t)n; }
API(WideCharToMultiByte){ int32_t n = (int32_t)a[3]; uint32_t s = a[2];
                   if (n < 0) { n = 0; while (RD16(s + 2*n)) n++; n++; }
                   if (a[5] == 0) return (uint32_t)n; if ((uint32_t)n > a[5]) { last_error = 122; return 0; }
                   for (int i = 0; i < n; i++) { uint16_t c = RD16(s + 2*i); G[a[4] + i] = c < 0x80 ? (uint8_t)c : '?'; }
                   if (a[7]) WR32(a[7], 0); return (uint32_t)n; }
static uint16_t ctype1(unsigned c) {
    uint16_t t = 0;
    if (c > 0x7f) return 0;
    if (isupper(c)) t |= 0x1|0x100; if (islower(c)) t |= 0x2|0x100; if (isdigit(c)) t |= 0x4;
    if (isspace(c)) t |= 0x8; if (ispunct(c)) t |= 0x10; if (iscntrl(c)) t |= 0x20;
    if (c == ' ' || c == '\t') t |= 0x40; if (isxdigit(c)) t |= 0x80; return t;
}
API(GetStringTypeA){ int32_t n = (int32_t)a[3]; const uint8_t *s = G + a[2]; if (n < 0) n = (int32_t)strlen((const char *)s) + 1;
                   for (int i = 0; i < n; i++) WR16(a[4] + 2*i, ctype1(s[i])); return 1; }
API(GetStringTypeW){ /* (dwInfoType, lpSrcStr, cchSrc, lpCharType) - no LCID */
                   int32_t n = (int32_t)a[2]; uint32_t s = a[1]; if (n < 0) { n = 0; while (RD16(s + 2*n)) n++; n++; }
                   for (int i = 0; i < n; i++) WR16(a[3] + 2*i, ctype1(RD16(s + 2*i))); return 1; }
static uint32_t lcmap(uint32_t flags, int wide, const uint32_t *a) {
    int32_t n = (int32_t)a[3]; uint32_t s = a[2], d = a[4];
    if (n < 0) { n = 0; while (wide ? RD16(s + 2*n) : G[s + n]) n++; n++; }
    if (a[5] == 0) return (uint32_t)n; if ((uint32_t)n > a[5]) { last_error = 122; return 0; }
    for (int i = 0; i < n; i++) {
        unsigned c = wide ? RD16(s + 2*i) : G[s + i];
        if (c < 0x80) { if (flags & 0x100) c = (unsigned)tolower(c); else if (flags & 0x200) c = (unsigned)toupper(c); }
        if (wide) WR16(d + 2*i, (uint16_t)c); else G[d + i] = (uint8_t)c;
    } return (uint32_t)n;
}
API(LCMapStringA){ return lcmap(a[1], 0, a); }
API(LCMapStringW){ return lcmap(a[1], 1, a); }

/* ---- USER32 / GDI32 (headless: a window is accepted, the message loop sees WM_QUIT immediately) ---- */
API(GetStockObject){ return 0x2001; }
API(RegisterClassA){ return 0xC001; }
API(GetSystemMetrics){ return a[0] == 0 ? 1024 : 768; }
API(CreateWindowExA){ if (trace_api) fprintf(stderr, "  [shim] CreateWindowExA title=%s\n", a[2] ? GSTR(a[2]) : ""); return 0x10001; }
API(ShowWindow)  { return 0; }
API(UpdateWindow){ return 1; }
API(GetMessageA) { return 0; }
API(TranslateMessage){ return 0; }
API(DispatchMessageA){ return 0; }
API(PostQuitMessage){ return 0; }
API(DefWindowProcA){ return 0; }

#define D(n, k) { #n, k, api_##n }
const ApiDef rt_shim_defs[] = {
 D(HeapCreate,3),D(HeapDestroy,1),D(GetProcessHeap,0),D(HeapAlloc,3),D(HeapFree,3),D(HeapSize,3),D(HeapValidate,3),D(HeapReAlloc,4),
 D(VirtualAlloc,4),D(VirtualFree,3),D(GlobalAlloc,2),D(GlobalFree,1),D(GlobalLock,1),D(GlobalUnlock,1),D(GlobalHandle,1),D(LocalFree,1),
 D(IsBadReadPtr,2),D(IsBadWritePtr,2),D(IsBadCodePtr,1),
 D(GetVersion,0),D(GetVersionExA,1),D(GetCommandLineA,0),D(GetModuleHandleA,1),D(GetModuleFileNameA,3),D(LoadLibraryA,1),D(FreeLibrary,1),
 D(GetProcAddress,2),D(GetStartupInfoA,1),D(GetCurrentProcess,0),D(GetCurrentThread,0),D(GetCurrentProcessId,0),D(GetCurrentThreadId,0),
 D(ExitProcess,1),D(TerminateProcess,2),D(UnhandledExceptionFilter,1),D(SetUnhandledExceptionFilter,1),D(RtlUnwind,4),
 D(GetLastError,0),D(SetLastError,1),D(SetErrorMode,1),D(SetHandleCount,1),D(CloseHandle,1),D(CreateMutexA,3),D(ReleaseMutex,1),
 D(WaitForSingleObject,2),D(GetExitCodeProcess,2),D(CreateProcessA,10),D(OutputDebugStringA,1),
 D(GetWindowsDirectoryA,2),D(GetSystemDirectoryA,2),D(GetCurrentDirectoryA,2),
 D(GetDiskFreeSpaceA,5),
 D(GetSystemTimeAsFileTime,1),
 D(CoInitialize,1),D(CoUninitialize,0),
 D(GetTickCount,0),D(timeGetTime,0),D(QueryPerformanceFrequency,1),D(QueryPerformanceCounter,1),D(Sleep,1),
 D(InitializeCriticalSection,1),D(DeleteCriticalSection,1),D(EnterCriticalSection,1),D(LeaveCriticalSection,1),
 D(InterlockedIncrement,1),D(InterlockedDecrement,1),D(InterlockedExchange,2),D(TlsAlloc,0),D(TlsFree,1),D(TlsGetValue,1),D(TlsSetValue,2),
 D(lstrlenA,1),D(lstrcpyA,2),D(lstrcatA,2),
 D(GetEnvironmentStrings,0),D(GetEnvironmentStringsW,0),D(FreeEnvironmentStringsA,1),D(FreeEnvironmentStringsW,1),D(GetEnvironmentVariableA,3),
 D(GetStdHandle,1),D(GetFileType,1),D(WriteFile,5),D(GetFileAttributesA,1),
 D(GetACP,0),D(GetOEMCP,0),D(GetCPInfo,2),D(MultiByteToWideChar,6),D(WideCharToMultiByte,8),D(GetStringTypeA,5),D(GetStringTypeW,4),
 D(LCMapStringA,6),D(LCMapStringW,6),
 D(GetStockObject,1),D(RegisterClassA,1),D(GetSystemMetrics,1),D(CreateWindowExA,12),D(ShowWindow,2),D(UpdateWindow,1),
 D(GetMessageA,4),D(TranslateMessage,1),D(DispatchMessageA,1),D(PostQuitMessage,1),D(DefWindowProcA,4),
 {0, 0, 0}
};

/* last host-API calls (always recorded, cheap): dumped by rt_trap so a trap shows what the guest was doing right before */
#define RING_N 64
static struct { const char *name; uint32_t a[4]; int n; uint32_t r; } ring[RING_N]; static unsigned ring_pos;
static void ring_note_begin(const char *nm) { (void)nm; }
static void ring_note_end(const char *nm, const uint32_t *a, int n, uint32_t r) { unsigned i = ring_pos++ % RING_N; ring[i].name = nm; ring[i].n = n > 4 ? 4 : n; for (int k = 0; k < ring[i].n; k++) ring[i].a[k] = a[k]; ring[i].r = r; }
void rt_dump_recent_api(int count) {
    if (count > RING_N) count = RING_N; rt_log("  last %d host API calls (oldest first):", count);
    for (int k = count; k >= 1; k--) { if (ring_pos < (unsigned)k) continue; unsigned i = (ring_pos - k) % RING_N; if (!ring[i].name) continue;
        char b[160]; int o = snprintf(b, sizeof b, "    %s(", ring[i].name); for (int j = 0; j < ring[i].n; j++) o += snprintf(b + o, sizeof b - o, "%s%x", j ? "," : "", ring[i].a[j]);
        snprintf(b + o, sizeof b - o, ") = %x", ring[i].r); rt_log("%s", b); } }
void api_dispatch(uint32_t id) {
    static int init; static const ApiDef *map[8192];
    if (!init) { init = 1; trace_api = getenv("RT_API_TRACE") != NULL; }
    if (id >= 8192 || (int)id >= rt_api_count()) rt_trap(id, "call to invalid import id");
    if (!map[id] && rt_api_def((int)id)) map[id] = rt_api_def((int)id);
    if (!map[id]) {
        const char *nm = rt_api_name((int)id);
        if (nm) { for (const ApiDef *d = rt_host_audio_defs; d->name && !map[id]; d++) if (!strcmp(d->name, nm)) map[id] = d;
                  for (const ApiDef *d = rt_host_dmusic_defs; d->name && !map[id]; d++) if (!strcmp(d->name, nm)) map[id] = d;
                  for (const ApiDef *d = rt_host_ddraw_defs; d->name && !map[id]; d++) if (!strcmp(d->name, nm)) map[id] = d;
                  for (const ApiDef *d = rt_host_gdi_defs; d->name && !map[id]; d++) if (!strcmp(d->name, nm)) map[id] = d;
                  for (const ApiDef *d = rt_host_dinput_defs; d->name && !map[id]; d++) if (!strcmp(d->name, nm)) map[id] = d;
                  for (const ApiDef *d = rt_host_threads_defs; d->name && !map[id]; d++) if (!strcmp(d->name, nm)) map[id] = d;
                  for (const ApiDef *d = rt_host_res_defs; d->name && !map[id]; d++) if (!strcmp(d->name, nm)) map[id] = d;
                  for (const ApiDef *d = rt_host_user32_defs; d->name && !map[id]; d++) if (!strcmp(d->name, nm)) map[id] = d;
                  for (const ApiDef *d = rt_host_defs; d->name && !map[id]; d++) if (!strcmp(d->name, nm)) map[id] = d;
                  for (const ApiDef *d = rt_shim_defs; d->name && !map[id]; d++) if (!strcmp(d->name, nm)) map[id] = d;
                  for (const ApiDef *d = rt_stub_defs; d->name && !map[id]; d++) if (!strcmp(d->name, nm)) map[id] = d; }
        if (!map[id]) { static char msg[200]; snprintf(msg, sizeof msg, "import %s!%s is not provided (ordinal import or missing from api_stubs_generated.c)", rt_api_dll((int)id), nm ? nm : "#ordinal"); rt_trap(0, msg); }
    }
    const ApiDef *d = map[id];
    ring_note_begin(d->name);
    if (d->nargs < -1) { static char msg[200]; snprintf(msg, sizeof msg, "%s: argument count unknown, add it to winapi_sigs.py", d->name); rt_trap(0, msg); }
    int n = d->nargs < 0 ? 0 : d->nargs;
    const uint32_t *args = (const uint32_t *)(G + ESP + 4);
    uint32_t a16[16]; memcpy(a16, args, 4 * (n > 16 ? 16 : n));
    if (d->nargs == -1) memcpy(a16, args, 4 * 8);            /* cdecl varargs: expose the first 8 dwords */
    uint32_t r = d->fn(a16);
    ring_note_end(d->name, a16, n, r);
    if (trace_api) { char b[200]; int o = snprintf(b, sizeof b, "API %s(", d->name);
        for (int i = 0; i < n && i < 8 && o < 170; i++) o += snprintf(b + o, sizeof b - o, "%s%x", i ? "," : "", a16[i]);
        snprintf(b + o, sizeof b - o, ") = %x", r); rt_log("%s", b); }
    EAX = r; ESP += 4 + 4 * n;
}
