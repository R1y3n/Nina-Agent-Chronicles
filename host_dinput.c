/* host_dinput.c - DirectInput (DX3-5 "A" interfaces) emulation.  The engine sees a system keyboard and a system mouse; both are
 * synthesised from the game controller by input_map.c (left stick = WASD, right stick = mouse look, triggers = mouse buttons ...).
 * No joystick is reported, so the engine always uses its keyboard+mouse bindings.
 * COM objects live in guest memory: [0]=vtable pointer, [4]=refcount, [8]=host index.  Every vtable slot is a host function
 * minted with rt_host_func(), so guest code calls straight through the vtable (stdcall, `this` first). */
#include "rt.h"
#include "input_map.h"
#include <time.h>
#include <strings.h>
#define GSTR(p) ((const char *)(G + (p)))
#define API(n) static uint32_t api_##n(const uint32_t *a)
#define D(n, k) { #n, k, api_##n }

#define DI_OK 0
#define DI_BUFFEROVERFLOW 1
#define E_NOTIMPL 0x80004001u
#define E_NOINTERFACE 0x80004002u
#define DIERR_DEVICENOTREG 0x80040154u
#define DIERR_INVALIDPARAM 0x80070057u

static int di_trace(void) { static int t = -1; if (t < 0) t = getenv("RT_DI_TRACE") != NULL; return t; }
#define TR(...) do { if (di_trace()) rt_log("[dinput] " __VA_ARGS__); } while (0)

/* ---- platform pad (ps4_pad.c on the console; neutral on PC unless host_pad_read is provided) ---- */
__attribute__((weak)) int host_pad_read(PadSample *p) { (void)p; return 0; }

typedef struct { uint32_t ofs, data, ts, seq; } Ev;
typedef struct { int kind; int acquired; uint32_t datasize, bufsize, event; Ev q[512]; int qh, qt; int overflow; uint8_t pk[256], pm[8]; int32_t pdx, pdy, pwheel; uint32_t seq; } Dev;   /* kind 1 = keyboard, 2 = mouse */
static Dev devs[4]; static int ndevs;
static InputMap imap; static InputState cur; static int imap_ready; static uint64_t last_ms;

static uint64_t now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000; }
static void push(Dev *d, uint32_t ofs, uint32_t data) {
    int n = (d->qt + 1) % 512; if (n == d->qh) { d->qh = (d->qh + 1) % 512; d->overflow = 1; }
    d->q[d->qt] = (Ev){ ofs, data, (uint32_t)now_ms(), d->seq++ }; d->qt = n;
}
int host_set_event(uint32_t h);
void host_input_pump(void) {
    if (!ndevs) return;
    if (!imap_ready) { input_map_defaults(&imap);
#ifdef RT_ORBIS
        input_map_load(&imap, "/data/nina/input.cfg");
#else
        input_map_load(&imap, getenv("NINA_INPUT_CFG") ? getenv("NINA_INPUT_CFG") : "input.cfg");
#endif
        imap_ready = 1; last_ms = now_ms(); }
    uint64_t t = now_ms(); int dt = (int)(t - last_ms); if (dt < 1) return; if (dt > 100) dt = 100; last_ms = t;
    PadSample p = { 0, 128, 128, 128, 128, 0, 0 }; host_pad_read(&p);
    cur.dx = cur.dy = cur.wheel = 0; input_map_apply(&imap, &p, dt, &cur);
#ifndef RT_ORBIS
    host_gui_input(cur.key, &cur.dx, &cur.dy, cur.mbtn);
#endif
    for (int i = 0; i < ndevs; i++) {
        Dev *d = &devs[i]; int fresh = 0;
        if (d->kind == 1) {
            for (int k = 0; k < 256; k++) if (cur.key[k] != d->pk[k]) { if (d->bufsize) { push(d, (uint32_t)k, cur.key[k]); fresh = 1; } d->pk[k] = cur.key[k]; }
        } else {
            d->pdx += cur.dx; d->pdy += cur.dy; d->pwheel += cur.wheel;
            if (d->bufsize) {
                if (cur.dx) { push(d, 0, (uint32_t)cur.dx); fresh = 1; } if (cur.dy) { push(d, 4, (uint32_t)cur.dy); fresh = 1; } if (cur.wheel) { push(d, 8, (uint32_t)cur.wheel); fresh = 1; }
                for (int b = 0; b < 8; b++) if (cur.mbtn[b] != d->pm[b]) { push(d, 12 + (uint32_t)b, cur.mbtn[b]); fresh = 1; }
                if (cur.dx || cur.dy || cur.wheel) { d->pdx = d->pdy = d->pwheel = 0; }
            }
            memcpy(d->pm, cur.mbtn, 8);
        }
        if (fresh && d->event) host_set_event(d->event);
    }
}

/* ---- COM plumbing ---- */
static uint32_t vt_di, vt_dev;
static uint32_t new_obj(uint32_t vt, int idx) { uint32_t o = rt_guest_alloc(16); WR32(o, vt); WR32(o + 4, 1); WR32(o + 8, (uint32_t)idx); return o; }
static Dev *dev_of(uint32_t obj) { uint32_t i = RD32(obj + 8); return i < 4 ? &devs[i] : NULL; }

API(com_QueryInterface) { if (a[2]) { WR32(a[2], a[0]); WR32(a[0] + 4, RD32(a[0] + 4) + 1); } return DI_OK; }
API(com_AddRef) { uint32_t r = RD32(a[0] + 4) + 1; WR32(a[0] + 4, r); return r; }
API(com_Release) { uint32_t r = RD32(a[0] + 4); if (r) r--; WR32(a[0] + 4, r); return r; }
API(com_notimpl) { (void)a; return E_NOTIMPL; }
API(com_ok) { (void)a; return DI_OK; }

static void fill_instance(uint32_t p, int kind) {                             /* DIDEVICEINSTANCEA (DX5+ layout, 0x244 bytes) */
    memset(G + p, 0, 0x244); WR32(p, 0x244);
    WR32(p + 4, kind == 1 ? 0x6F1D2B61u : 0x6F1D2B60u); WR16(p + 8, 0xD5A0); WR16(p + 10, 0x11CF); { static const uint8_t t[8] = { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0, 0 }; memcpy(G + p + 12, t, 8); }
    memcpy(G + p + 20, G + p + 4, 16);
    WR32(p + 36, kind == 1 ? 0x0403u : 0x0102u);                                 /* DIDEVTYPE_KEYBOARD / DIDEVTYPE_MOUSE (+ subtype) */
    snprintf((char *)G + p + 40, 260, "%s", kind == 1 ? "Keyboard" : "Mouse"); snprintf((char *)G + p + 300, 260, "%s", kind == 1 ? "Keyboard" : "Mouse");
}
/* IDirectInputA */
API(di_CreateDevice) {
    uint32_t g = RD32(a[1]); int kind = g == 0x6F1D2B61u ? 1 : g == 0x6F1D2B60u ? 2 : 0;
    TR("CreateDevice(guid=%08x) -> %s", g, kind == 1 ? "keyboard" : kind == 2 ? "mouse" : "none");
    if (!kind) return DIERR_DEVICENOTREG; if (ndevs >= 4) return DIERR_INVALIDPARAM;
    memset(&devs[ndevs], 0, sizeof devs[ndevs]); devs[ndevs].kind = kind; devs[ndevs].datasize = kind == 1 ? 256 : 16;
    WR32(a[2], new_obj(vt_dev, ndevs)); ndevs++; return DI_OK;
}
API(di_CreateDeviceEx) {                                                       /* (this, rguid, riid, ppvOut, punkOuter) - IDirectInput7 */
    uint32_t args[4] = { a[0], a[1], a[3], a[4] }; return api_di_CreateDevice(args);
}
API(di_EnumDevices) {                                                         /* (this, devType, callback, ref, flags) */
    uint32_t type = a[1], inst = rt_scratch_alloc(0x240); TR("EnumDevices(type=%u)", type);
    for (int kind = 1; kind <= 2; kind++) {
        uint32_t want = kind == 1 ? 3 : 2; if (type && type != want) continue;
        fill_instance(inst, kind); uint32_t args[2] = { inst, a[3] };
        if (!rt_call_guest(a[2], 2, args)) break;                             /* DIENUM_STOP */
    }
    return DI_OK;
}
API(di_Initialize) { (void)a; return DI_OK; }
/* IDirectInputDeviceA */
API(dev_GetCapabilities) { Dev *d = dev_of(a[0]); memset(G + a[1] + 4, 0, RD32(a[1]) - 4); WR32(a[1] + 4, 1); WR32(a[1] + 8, d && d->kind == 1 ? 0x0403u : 0x0102u);
    WR32(a[1] + 12, d && d->kind == 2 ? 3 : 0); WR32(a[1] + 16, d && d->kind == 1 ? 128 : 4); return DI_OK; }
API(dev_SetProperty) { Dev *d = dev_of(a[0]); uint32_t prop = a[1] < 0x10000 ? a[1] : 0;
    if (d && prop == 1) { d->bufsize = RD32(a[2] + 16); TR("SetProperty(BUFFERSIZE=%u) on %s", d->bufsize, d->kind == 1 ? "kb" : "mouse"); } return DI_OK; }
API(dev_GetProperty) { return E_NOTIMPL; }
API(dev_Acquire) { Dev *d = dev_of(a[0]); if (d) d->acquired = 1; return DI_OK; }
API(dev_Unacquire) { Dev *d = dev_of(a[0]); if (d) d->acquired = 0; return DI_OK; }
API(dev_SetDataFormat) { Dev *d = dev_of(a[0]); if (d && a[1]) { d->datasize = RD32(a[1] + 12); TR("SetDataFormat(datasize=%u) on %s", d->datasize, d->kind == 1 ? "kb" : "mouse"); } return DI_OK; }
API(dev_SetEventNotification) { Dev *d = dev_of(a[0]); if (d) d->event = a[1]; TR("SetEventNotification(%x)", a[1]); return DI_OK; }
API(dev_GetDeviceState) {                                                     /* (this, cb, data) */
    Dev *d = dev_of(a[0]); if (!d) return DIERR_INVALIDPARAM; host_input_pump(); uint32_t cb = a[1];
    if (d->kind == 1) { memset(G + a[2], 0, cb); memcpy(G + a[2], cur.key, cb < 256 ? cb : 256); }
    else {
        memset(G + a[2], 0, cb);
        if (cb >= 12) { WR32(a[2], (uint32_t)d->pdx); WR32(a[2] + 4, (uint32_t)d->pdy); WR32(a[2] + 8, (uint32_t)d->pwheel); }
        for (uint32_t b = 0; b < 8 && 12 + b < cb; b++) G[a[2] + 12 + b] = cur.mbtn[b];
        d->pdx = d->pdy = d->pwheel = 0;                                       /* relative axes are consumed */
    }
    return DI_OK;
}
API(dev_GetDeviceData) {                                                      /* (this, cbObjData, rgdod, pdwInOut, flags) */
    Dev *d = dev_of(a[0]); if (!d) return DIERR_INVALIDPARAM; host_input_pump();
    uint32_t want = a[3] ? RD32(a[3]) : 0, n = 0; int peek = (a[4] & 1) != 0, qh = d->qh;
    while (qh != d->qt && (a[2] ? n < want : 1)) {
        if (a[2]) { uint32_t o = a[2] + n * 16; WR32(o, d->q[qh].ofs); WR32(o + 4, d->q[qh].data); WR32(o + 8, d->q[qh].ts); WR32(o + 12, d->q[qh].seq); }
        qh = (qh + 1) % 512; n++;
    }
    if (!peek) d->qh = qh; if (a[3]) WR32(a[3], n);
    uint32_t r = d->overflow ? DI_BUFFEROVERFLOW : DI_OK; if (!peek) d->overflow = 0; return r;
}
API(dev_GetDeviceInfo) { Dev *d = dev_of(a[0]); fill_instance(a[1], d ? d->kind : 1); return DI_OK; }
API(dev_Poll) { host_input_pump(); return DI_OK; }

API(DirectInputCreateA) {                                                     /* (hinst, version, ppDI, punkOuter) */
    TR("DirectInputCreateA(version=0x%x)", a[1]);
    if (!vt_di) {
        uint32_t qi = rt_host_func("IDirectInput::QueryInterface", 3, api_com_QueryInterface), ar = rt_host_func("IDirectInput::AddRef", 1, api_com_AddRef), rl = rt_host_func("IDirectInput::Release", 1, api_com_Release);
        uint32_t ni[8] = { 0 };
        static const uint8_t di_na[12] = { 3,1,1,4,5,2,3,3,1,5,1,1 };
        static const uint8_t dev_na[27] = { 3,1,1,2,4,3,3,1,1,3,5,2,2,3,4,2,3,4,5,2,4,2,5,4,1,5,1 };
        for (int n = 1; n <= 7; n++) ni[n] = rt_host_func("IDirectInput::unimplemented", n, api_com_notimpl);
        uint32_t t[8] = { qi, ar, rl, rt_host_func("IDirectInput::CreateDevice", 4, api_di_CreateDevice), rt_host_func("IDirectInput::EnumDevices", 5, api_di_EnumDevices),
                          rt_host_func("IDirectInput::GetDeviceStatus", 2, api_com_ok), rt_host_func("IDirectInput::RunControlPanel", 3, api_com_ok), rt_host_func("IDirectInput::Initialize", 3, api_di_Initialize) };
        vt_di = rt_guest_alloc(4 * 12); for (int i = 0; i < 8; i++) WR32(vt_di + 4 * i, t[i]); for (int i = 8; i < 12; i++) WR32(vt_di + 4 * i, ni[di_na[i]]);
        WR32(vt_di + 4 * 9, rt_host_func("IDirectInput7::CreateDeviceEx", 5, api_di_CreateDeviceEx));
        uint32_t d[27]; d[0] = qi; d[1] = ar; d[2] = rl;
        d[3] = rt_host_func("IDirectInputDevice::GetCapabilities", 2, api_dev_GetCapabilities); d[4] = rt_host_func("IDirectInputDevice::EnumObjects", 4, api_com_ok);
        d[5] = rt_host_func("IDirectInputDevice::GetProperty", 3, api_dev_GetProperty); d[6] = rt_host_func("IDirectInputDevice::SetProperty", 3, api_dev_SetProperty);
        d[7] = rt_host_func("IDirectInputDevice::Acquire", 1, api_dev_Acquire); d[8] = rt_host_func("IDirectInputDevice::Unacquire", 1, api_dev_Unacquire);
        d[9] = rt_host_func("IDirectInputDevice::GetDeviceState", 3, api_dev_GetDeviceState); d[10] = rt_host_func("IDirectInputDevice::GetDeviceData", 5, api_dev_GetDeviceData);
        d[11] = rt_host_func("IDirectInputDevice::SetDataFormat", 2, api_dev_SetDataFormat); d[12] = rt_host_func("IDirectInputDevice::SetEventNotification", 2, api_dev_SetEventNotification);
        d[13] = rt_host_func("IDirectInputDevice::SetCooperativeLevel", 3, api_com_ok); d[14] = rt_host_func("IDirectInputDevice::GetObjectInfo", 4, api_com_notimpl);
        d[15] = rt_host_func("IDirectInputDevice::GetDeviceInfo", 2, api_dev_GetDeviceInfo); d[16] = rt_host_func("IDirectInputDevice::RunControlPanel", 3, api_com_ok);
        d[17] = rt_host_func("IDirectInputDevice::Initialize", 4, api_com_ok);
        for (int i = 18; i < 27; i++) d[i] = ni[dev_na[i]];                      /* DX5 force-feedback / Escape / Poll / SendDeviceData */
        d[24] = rt_host_func("IDirectInputDevice2::Poll", 1, api_dev_Poll);
        vt_dev = rt_guest_alloc(4 * 27); for (int i = 0; i < 27; i++) WR32(vt_dev + 4 * i, d[i]);
    }
    WR32(a[2], new_obj(vt_di, 0)); return DI_OK;
}

const ApiDef rt_host_dinput_defs[] = { D(DirectInputCreateA,4), { 0, 0, 0 } };
