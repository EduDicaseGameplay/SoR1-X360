/* SDL2 subset for the Xbox 360 (X360 SoR1): the part of SDL2 the Streets of Rage runtime calls,
 * implemented over the XDK. The declarations are SDL2's own headers (third_party/sdl2/include), so the
 * runtime compiles unchanged; only the functions it uses exist here.
 *
 * This file: init, time, threads and sync, atomics, events, game controllers (XInput), keyboard and
 * mouse (none on the console), audio queue (XAudio2 through x360_snd.c), files and misc.
 * Video (window, renderer, textures) is sdl_x360_video.cpp. */
#include <xtl.h>
#include <xinput2.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SDL.h"
#include "x360_backend.h"

void x360_log_write(const char *text, unsigned length); /* x360_log.cpp */
void x360_install_crash_logger(void);

static void logline(const char *s) { x360_log_write(s, (unsigned)strlen(s)); x360_log_write("\n", 1); }

/* ---- errors ------------------------------------------------------------------------------------ */
static char g_error[256] = "";
const char *SDL_GetError(void) { return g_error; }
static int set_error(const char *e) { snprintf(g_error, sizeof g_error, "%s", e); return -1; }

/* ---- init -------------------------------------------------------------------------------------- */
static int g_audio_ok;
int SDL_Init(Uint32 flags) {
    static int once;
    if (!once) { once = 1; x360_install_crash_logger(); logline("[sdl] init"); }
    return SDL_InitSubSystem(flags);
}
int SDL_InitSubSystem(Uint32 flags) {
    if ((flags & SDL_INIT_AUDIO) && !g_audio_ok) {
        logline("[sdl] XAudio2Create");
        g_audio_ok = x360snd_init();
        logline(g_audio_ok ? "[sdl] audio ok" : "[sdl] audio failed");
    }
    return 0;
}
void SDL_Quit(void) { logline("[sdl] quit"); }
void SDL_free(void *p) { free(p); }
int SDL_GetCPUCount(void) { return 6; } /* 3 cores x 2 hardware threads */

/* ---- time -------------------------------------------------------------------------------------- */
Uint64 SDL_GetPerformanceCounter(void) { LARGE_INTEGER c; QueryPerformanceCounter(&c); return (Uint64)c.QuadPart; }
Uint64 SDL_GetPerformanceFrequency(void) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return (Uint64)f.QuadPart; }
Uint64 SDL_GetTicks64(void) { return (Uint64)GetTickCount(); }
void SDL_Delay(Uint32 ms) { Sleep(ms); }

/* ---- threads ----------------------------------------------------------------------------------- */
struct SDL_Thread { HANDLE h; SDL_ThreadFunction fn; void *data; int status; };
static DWORD WINAPI thread_main(LPVOID p) {
    SDL_Thread *t = (SDL_Thread *)p;
    t->status = t->fn(t->data);
    return 0;
}
/* SDL_CreateThread is a macro over SDL_CreateThreadRuntime on Windows-like builds (SDL_thread.h);
 * both spellings are provided. Workers go round the hardware threads 1..5 (0 is the main thread's). */
static SDL_Thread *create_thread(SDL_ThreadFunction fn, const char *name, void *data) {
    static LONG next_hw = 0;
    SDL_Thread *t = (SDL_Thread *)calloc(1, sizeof *t);
    if (!t) return NULL;
    t->fn = fn; t->data = data;
    t->h = CreateThread(NULL, 256 * 1024, thread_main, t, CREATE_SUSPENDED, NULL);
    if (!t->h) { free(t); set_error("CreateThread failed"); return NULL; }
    /* The sound chip thread (sound.cpp) gets core 1 to itself (the input thread there
     * sleeps nearly all the time); the game runs on core 0, XAudio2 on core 2. */
    if (name && !strcmp(name, "ym")) XSetThreadProcessor(t->h, 2);
    else XSetThreadProcessor(t->h, 1 + (int)(InterlockedIncrement(&next_hw) % 5));
    ResumeThread(t->h);
    return t;
}
/* SDL_thread.h on Windows-like builds turns SDL_CreateThread(fn, name, data) into a five-argument call
 * passing the CRT's thread starters; they are not needed here. */
#undef SDL_CreateThread
DECLSPEC SDL_Thread *SDLCALL SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *data,
                                              pfnSDL_CurrentBeginThread b, pfnSDL_CurrentEndThread e) {
    (void)b; (void)e;
    return create_thread(fn, name, data);
}
void SDL_WaitThread(SDL_Thread *t, int *status) {
    if (!t) return;
    WaitForSingleObject(t->h, INFINITE);
    CloseHandle(t->h);
    if (status) *status = t->status;
    free(t);
}
void SDL_DetachThread(SDL_Thread *t) { if (t) { CloseHandle(t->h); /* the struct stays: the thread reads it */ } }

/* ---- mutex, semaphore, atomics ------------------------------------------------------------------ */
struct SDL_mutex { CRITICAL_SECTION cs; };
SDL_mutex *SDL_CreateMutex(void) {
    SDL_mutex *m = (SDL_mutex *)malloc(sizeof *m);
    if (m) InitializeCriticalSection(&m->cs);
    return m;
}
int SDL_LockMutex(SDL_mutex *m) { if (m) EnterCriticalSection(&m->cs); return 0; }
int SDL_UnlockMutex(SDL_mutex *m) { if (m) LeaveCriticalSection(&m->cs); return 0; }
void SDL_DestroyMutex(SDL_mutex *m) { if (m) { DeleteCriticalSection(&m->cs); free(m); } }

struct SDL_semaphore { HANDLE h; };
SDL_sem *SDL_CreateSemaphore(Uint32 initial) {
    SDL_sem *s = (SDL_sem *)malloc(sizeof *s);
    if (s) s->h = CreateSemaphore(NULL, (LONG)initial, 0x7FFFFFFF, NULL);
    return s;
}
int SDL_SemWait(SDL_sem *s) { return s && WaitForSingleObject(s->h, INFINITE) == WAIT_OBJECT_0 ? 0 : -1; }
int SDL_SemPost(SDL_sem *s) { return s && ReleaseSemaphore(s->h, 1, NULL) ? 0 : -1; }
void SDL_DestroySemaphore(SDL_sem *s) { if (s) { CloseHandle(s->h); free(s); } }

/* Interlocked* carry full barriers (lwsync) on the Xenon. */
int SDL_AtomicSet(SDL_atomic_t *a, int v) { return (int)InterlockedExchange((LONG volatile *)&a->value, v); }
int SDL_AtomicGet(SDL_atomic_t *a) { return (int)InterlockedCompareExchange((LONG volatile *)&a->value, 0, 0); }
int SDL_AtomicAdd(SDL_atomic_t *a, int v) { return (int)InterlockedExchangeAdd((LONG volatile *)&a->value, v); }
SDL_bool SDL_AtomicCAS(SDL_atomic_t *a, int oldv, int newv) {
    return InterlockedCompareExchange((LONG volatile *)&a->value, newv, oldv) == oldv ? SDL_TRUE : SDL_FALSE;
}

/* ---- game controllers (XInput) ------------------------------------------------------------------ */
/* One SDL_GameController per console port; its instance id is the port. A port with nothing in it is
 * asked again only every 2 s (XInputGetState on an empty port is slow: KI-X360). */
struct _SDL_GameController { int port; };
struct _SDL_Joystick { int port; };
typedef struct {
    int connected;
    DWORD last_try;
    XINPUT_STATE st;
    Uint8 buttons[SDL_CONTROLLER_BUTTON_MAX];
    Sint16 axes[SDL_CONTROLLER_AXIS_MAX];
    struct _SDL_GameController gc;
    struct _SDL_Joystick js;
    int reported; /* DEVICEADDED sent */
} Pad;
static Pad g_pads[XUSER_MAX_COUNT];
static int g_controller_events = 1;

static const WORD k_xbtn[SDL_CONTROLLER_BUTTON_MAX] = {
    /* A B X Y BACK GUIDE START LSTICK RSTICK LSHOULDER RSHOULDER UP DOWN LEFT RIGHT */
    XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y, XINPUT_GAMEPAD_BACK, 0,
    XINPUT_GAMEPAD_START, XINPUT_GAMEPAD_LEFT_THUMB, XINPUT_GAMEPAD_RIGHT_THUMB, XINPUT_GAMEPAD_LEFT_SHOULDER,
    XINPUT_GAMEPAD_RIGHT_SHOULDER, XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_DOWN, XINPUT_GAMEPAD_DPAD_LEFT,
    XINPUT_GAMEPAD_DPAD_RIGHT,
};

/* ---- events -------------------------------------------------------------------------------------- */
#define EVQ 128
static SDL_Event g_evq[EVQ];
static int g_evhead, g_evtail;
static void push_event(const SDL_Event *e) {
    int next = (g_evtail + 1) % EVQ;
    if (next == g_evhead) return; /* full: drop */
    g_evq[g_evtail] = *e;
    g_evtail = next;
}

/* Back + Start held 3 s: leave the game (KI, BRE). The runtime sees SDL_QUIT. */
static DWORD g_combo_since;
static int g_quit_sent;

static Sint16 axis_value(const XINPUT_GAMEPAD *g, int a) {
    switch (a) {
    case SDL_CONTROLLER_AXIS_LEFTX: return g->sThumbLX;
    case SDL_CONTROLLER_AXIS_LEFTY: return g->sThumbLY == -32768 ? 32767 : (Sint16)-g->sThumbLY; /* SDL: down is + */
    case SDL_CONTROLLER_AXIS_RIGHTX: return g->sThumbRX;
    case SDL_CONTROLLER_AXIS_RIGHTY: return g->sThumbRY == -32768 ? 32767 : (Sint16)-g->sThumbRY;
    case SDL_CONTROLLER_AXIS_TRIGGERLEFT: return (Sint16)(g->bLeftTrigger * 32767 / 255);
    case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: return (Sint16)(g->bRightTrigger * 32767 / 255);
    }
    return 0;
}

static void poll_pads(void) {
    DWORD now = GetTickCount();
    for (int p = 0; p < XUSER_MAX_COUNT; p++) {
        Pad *pad = &g_pads[p];
        pad->gc.port = pad->js.port = p;
        if (!pad->connected && pad->last_try && now - pad->last_try < 2000) continue;
        pad->last_try = now;
        XINPUT_STATE st;
        DWORD r = XInputGetState((DWORD)p, &st);
        SDL_Event e;
        memset(&e, 0, sizeof e);
        if (r != ERROR_SUCCESS) {
            if (pad->connected) {
                pad->connected = 0;
                memset(pad->buttons, 0, sizeof pad->buttons);
                memset(pad->axes, 0, sizeof pad->axes);
                if (pad->reported) { e.type = SDL_CONTROLLERDEVICEREMOVED; e.cdevice.which = p; push_event(&e); }
                pad->reported = 0;
            }
            continue;
        }
        if (!pad->connected) {
            pad->connected = 1;
            pad->st = st;
            e.type = SDL_CONTROLLERDEVICEADDED; e.cdevice.which = p; push_event(&e);
            pad->reported = 1;
        }
        pad->st = st;
        for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) {
            Uint8 down = k_xbtn[b] && (st.Gamepad.wButtons & k_xbtn[b]) ? 1 : 0;
            if (down != pad->buttons[b]) {
                pad->buttons[b] = down;
                if (g_controller_events) {
                    memset(&e, 0, sizeof e);
                    e.type = down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
                    e.cbutton.which = p; e.cbutton.button = (Uint8)b; e.cbutton.state = down;
                    push_event(&e);
                }
            }
        }
        for (int a = 0; a < SDL_CONTROLLER_AXIS_MAX; a++) {
            Sint16 v = axis_value(&st.Gamepad, a);
            if (v != pad->axes[a]) {
                pad->axes[a] = v;
                if (g_controller_events) {
                    memset(&e, 0, sizeof e);
                    e.type = SDL_CONTROLLERAXISMOTION;
                    e.caxis.which = p; e.caxis.axis = (Uint8)a; e.caxis.value = v;
                    push_event(&e);
                }
            }
        }
        if (p == 0) {
            WORD both = XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_START;
            if ((st.Gamepad.wButtons & both) == both) {
                if (!g_combo_since) g_combo_since = now ? now : 1;
                else if (now - g_combo_since >= 3000 && !g_quit_sent) {
                    memset(&e, 0, sizeof e); e.type = SDL_QUIT; push_event(&e); g_quit_sent = 1;
                }
            } else g_combo_since = 0;
        }
    }
}

/* The pads are read on a thread of their own every 4 ms (250 Hz), not once a displayed frame: a
 * press and release between two frames still reaches the game and the menus as two events, however
 * slow a frame is. The queue and the pad states are shared under one lock. */
static CRITICAL_SECTION g_evcs;
static LONG g_input_started;
static DWORD WINAPI input_thread(LPVOID p) {
    (void)p;
    for (;;) {
        EnterCriticalSection(&g_evcs);
        poll_pads();
        LeaveCriticalSection(&g_evcs);
        Sleep(4);
    }
}
static void start_input(void) {
    if (InterlockedCompareExchange(&g_input_started, 1, 0) != 0) return;
    InitializeCriticalSection(&g_evcs);
    EnterCriticalSection(&g_evcs);
    poll_pads(); /* pads present at boot are there before the first frame */
    LeaveCriticalSection(&g_evcs);
    HANDLE h = CreateThread(NULL, 64 * 1024, input_thread, NULL, CREATE_SUSPENDED, NULL);
    if (h) { XSetThreadProcessor(h, 2); ResumeThread(h); CloseHandle(h); }
}

void SDL_PumpEvents(void) { start_input(); }
int SDL_PollEvent(SDL_Event *e) {
    int got = 0;
    start_input();
    EnterCriticalSection(&g_evcs);
    if (g_evhead != g_evtail) {
        if (e) *e = g_evq[g_evhead];
        g_evhead = (g_evhead + 1) % EVQ;
        got = 1;
    }
    LeaveCriticalSection(&g_evcs);
    return got;
}
void SDL_FlushEvents(Uint32 minType, Uint32 maxType) {
    SDL_Event keep[EVQ];
    int n = 0;
    start_input();
    EnterCriticalSection(&g_evcs);
    while (g_evhead != g_evtail) {
        SDL_Event *e = &g_evq[g_evhead];
        if (e->type < minType || e->type > maxType) keep[n++] = *e;
        g_evhead = (g_evhead + 1) % EVQ;
    }
    for (int i = 0; i < n; i++) push_event(&keep[i]);
    LeaveCriticalSection(&g_evcs);
}
int SDL_GameControllerEventState(int state) {
    if (state == SDL_ENABLE || state == SDL_IGNORE) g_controller_events = state == SDL_ENABLE;
    return g_controller_events ? SDL_ENABLE : SDL_IGNORE;
}
void SDL_GameControllerUpdate(void) { start_input(); } /* the input thread keeps the states current */

int SDL_NumJoysticks(void) { return XUSER_MAX_COUNT; }
SDL_bool SDL_IsGameController(int i) { return i >= 0 && i < XUSER_MAX_COUNT && g_pads[i].connected ? SDL_TRUE : SDL_FALSE; }
SDL_JoystickID SDL_JoystickGetDeviceInstanceID(int i) { return i; }
SDL_GameController *SDL_GameControllerOpen(int i) {
    if (i < 0 || i >= XUSER_MAX_COUNT) return NULL;
    g_pads[i].gc.port = i;
    return &g_pads[i].gc;
}
void SDL_GameControllerClose(SDL_GameController *g) { (void)g; }
SDL_bool SDL_GameControllerGetAttached(SDL_GameController *g) { return g && g_pads[g->port].connected ? SDL_TRUE : SDL_FALSE; }
SDL_Joystick *SDL_GameControllerGetJoystick(SDL_GameController *g) { return g ? &g_pads[g->port].js : NULL; }
SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick *j) { return j ? j->port : -1; }
Uint8 SDL_GameControllerGetButton(SDL_GameController *g, SDL_GameControllerButton b) {
    if (!g || b < 0 || b >= SDL_CONTROLLER_BUTTON_MAX) return 0;
    return g_pads[g->port].buttons[b];
}
Sint16 SDL_GameControllerGetAxis(SDL_GameController *g, SDL_GameControllerAxis a) {
    if (!g || a < 0 || a >= SDL_CONTROLLER_AXIS_MAX) return 0;
    return g_pads[g->port].axes[a];
}
int SDL_GameControllerRumble(SDL_GameController *g, Uint16 lo, Uint16 hi, Uint32 ms) {
    XINPUT_VIBRATION v;
    (void)ms; /* the runtime stops it itself with a zero rumble */
    if (!g) return -1;
    v.wLeftMotorSpeed = lo; v.wRightMotorSpeed = hi;
    return XInputSetState((DWORD)g->port, &v) == ERROR_SUCCESS ? 0 : -1;
}

/* Names as SDL2 spells them: the runtime writes and reads its bindings in settings.ini with these. */
static const char *const k_button_names[SDL_CONTROLLER_BUTTON_MAX] = {
    "a", "b", "x", "y", "back", "guide", "start", "leftstick", "rightstick", "leftshoulder", "rightshoulder",
    "dpup", "dpdown", "dpleft", "dpright",
};
static const char *const k_axis_names[SDL_CONTROLLER_AXIS_MAX] = {
    "leftx", "lefty", "rightx", "righty", "lefttrigger", "righttrigger",
};
const char *SDL_GameControllerGetStringForButton(SDL_GameControllerButton b) {
    return b >= 0 && b < SDL_CONTROLLER_BUTTON_MAX ? k_button_names[b] : NULL;
}
const char *SDL_GameControllerGetStringForAxis(SDL_GameControllerAxis a) {
    return a >= 0 && a < SDL_CONTROLLER_AXIS_MAX ? k_axis_names[a] : NULL;
}
SDL_GameControllerButton SDL_GameControllerGetButtonFromString(const char *s) {
    for (int b = 0; s && b < SDL_CONTROLLER_BUTTON_MAX; b++) if (!_stricmp(s, k_button_names[b])) return (SDL_GameControllerButton)b;
    return SDL_CONTROLLER_BUTTON_INVALID;
}
SDL_GameControllerAxis SDL_GameControllerGetAxisFromString(const char *s) {
    if (s && (*s == '+' || *s == '-')) s++;
    for (int a = 0; s && a < SDL_CONTROLLER_AXIS_MAX; a++) if (!_stricmp(s, k_axis_names[a])) return (SDL_GameControllerAxis)a;
    return SDL_CONTROLLER_AXIS_INVALID;
}

/* Virtual joysticks only serve the PC's scripted tests (--press): none here. */
int SDL_JoystickAttachVirtualEx(const SDL_VirtualJoystickDesc *d) { (void)d; return set_error("no virtual joysticks"); }
int SDL_JoystickSetVirtualButton(SDL_Joystick *j, int b, Uint8 v) { (void)j; (void)b; (void)v; return -1; }

/* ---- keyboard and mouse: none on the console --------------------------------------------------- */
const Uint8 *SDL_GetKeyboardState(int *n) {
    static Uint8 keys[SDL_NUM_SCANCODES];
    if (n) *n = SDL_NUM_SCANCODES;
    return keys;
}
const char *SDL_GetScancodeName(SDL_Scancode s) { (void)s; return ""; }
SDL_Scancode SDL_GetScancodeFromName(const char *n) { (void)n; return SDL_SCANCODE_UNKNOWN; }
Uint32 SDL_GetMouseState(int *x, int *y) { if (x) *x = 0; if (y) *y = 0; return 0; }

/* ---- audio: SDL's queue model over one XAudio2 stream ------------------------------------------- */
/* The runtime queues 48 kHz stereo int16 (SDL_QueueAudio) and paces itself by the queue's size
 * (SDL_GetQueuedAudioSize). A ring holds what is queued; x360_snd.c's stream thread pulls from it in
 * 512-frame buffers (four in flight on the voice) and plays silence when it runs dry. */
#define RING_BYTES (48000 * 4) /* one second */
static Uint8 g_ring[RING_BYTES];
static Uint32 g_rhead, g_rcount;
static CRITICAL_SECTION g_rcs;
static void *g_stream;
static int g_paused = 1;

static void pull_audio(void *buffer, unsigned int frames) {
    Uint8 *out = (Uint8 *)buffer;
    Uint32 want = frames * 4, got = 0;
    EnterCriticalSection(&g_rcs);
    if (!g_paused) {
        got = want < g_rcount ? want : g_rcount;
        Uint32 first = got < RING_BYTES - g_rhead ? got : RING_BYTES - g_rhead;
        memcpy(out, g_ring + g_rhead, first);
        memcpy(out + first, g_ring, got - first);
        g_rhead = (g_rhead + got) % RING_BYTES;
        g_rcount -= got;
    }
    LeaveCriticalSection(&g_rcs);
    if (got < want) memset(out + got, 0, want - got);
}

SDL_AudioDeviceID SDL_OpenAudioDevice(const char *device, int iscapture, const SDL_AudioSpec *want,
                                      SDL_AudioSpec *have, int changes) {
    (void)device; (void)iscapture; (void)changes;
    if (g_stream) return set_error("audio already open"), 0;
    if (!g_audio_ok) g_audio_ok = x360snd_init();
    if (!g_audio_ok) return set_error("XAudio2Create failed"), 0;
    InitializeCriticalSection(&g_rcs);
    /* Native int16: the console's XAudio2 takes big-endian PCM, which is what AUDIO_S16SYS is here. */
    g_stream = x360snd_stream_create(48000, 2, 512);
    if (!g_stream) return set_error("no source voice"), 0;
    x360snd_stream_callback(g_stream, pull_audio);
    x360snd_stream_play(g_stream);
    if (have) {
        *have = *want;
        have->freq = 48000; have->format = AUDIO_S16SYS; have->channels = 2; have->samples = 512;
    }
    logline("[sdl] audio open: XAudio2 48000 Hz stereo");
    return 1;
}
void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause) { (void)dev; g_paused = pause; }
void SDL_CloseAudioDevice(SDL_AudioDeviceID dev) {
    (void)dev;
    if (g_stream) { x360snd_stream_destroy(g_stream); g_stream = NULL; }
}
int SDL_QueueAudio(SDL_AudioDeviceID dev, const void *data, Uint32 len) {
    (void)dev;
    if (!g_stream) return -1;
    EnterCriticalSection(&g_rcs);
    if (len > RING_BYTES - g_rcount) len = RING_BYTES - g_rcount; /* full: the rest is dropped */
    len &= ~3u;
    Uint32 tail = (g_rhead + g_rcount) % RING_BYTES;
    Uint32 first = len < RING_BYTES - tail ? len : RING_BYTES - tail;
    memcpy(g_ring + tail, data, first);
    memcpy(g_ring, (const Uint8 *)data + first, len - first);
    g_rcount += len;
    LeaveCriticalSection(&g_rcs);
    return 0;
}
Uint32 SDL_GetQueuedAudioSize(SDL_AudioDeviceID dev) {
    (void)dev;
    return g_stream ? g_rcount : 0;
}
void SDL_ClearQueuedAudio(SDL_AudioDeviceID dev) {
    (void)dev;
    if (!g_stream) return;
    EnterCriticalSection(&g_rcs);
    g_rhead = g_rcount = 0;
    LeaveCriticalSection(&g_rcs);
}

/* ---- files and misc ------------------------------------------------------------------------------ */
/* game:\ is the folder default.xex runs from. */
char *SDL_GetBasePath(void) { return _strdup("game:\\"); }

int SDL_ShowSimpleMessageBox(Uint32 flags, const char *title, const char *message, SDL_Window *w) {
    (void)flags; (void)w;
    logline("[sdl] message box:");
    logline(title ? title : "");
    logline(message ? message : "");
    return 0;
}

/* Screenshots (F12 and the PC tests) write BMPs through SDL_RWops: not on the console. */
SDL_RWops *SDL_RWFromFile(const char *file, const char *mode) { (void)file; (void)mode; set_error("no RWops"); return NULL; }
int SDL_SaveBMP_RW(SDL_Surface *s, SDL_RWops *dst, int freedst) { (void)s; (void)dst; (void)freedst; return -1; }
SDL_Surface *SDL_CreateRGBSurfaceWithFormatFrom(void *pixels, int w, int h, int depth, int pitch, Uint32 format) {
    SDL_Surface *s = (SDL_Surface *)calloc(1, sizeof *s);
    (void)depth; (void)format;
    if (s) { s->pixels = pixels; s->w = w; s->h = h; s->pitch = pitch; s->flags = SDL_PREALLOC; }
    return s;
}
void SDL_FreeSurface(SDL_Surface *s) { free(s); }

/* Display queries: one display, the console's video mode (set by the renderer). */
int x360_video_width(void);
int x360_video_height(void);
int SDL_GetDisplayUsableBounds(int i, SDL_Rect *r) {
    (void)i;
    r->x = r->y = 0; r->w = x360_video_width(); r->h = x360_video_height();
    return 0;
}
int SDL_GetCurrentDisplayMode(int i, SDL_DisplayMode *m) {
    (void)i;
    memset(m, 0, sizeof *m);
    m->format = SDL_PIXELFORMAT_XRGB8888; m->w = x360_video_width(); m->h = x360_video_height(); m->refresh_rate = 60;
    return 0;
}

/* SDL_endian.h uses the MSVC intrinsics under _MSC_VER; the XDK CRT does not export them to clang. */
unsigned short _byteswap_ushort(unsigned short v) { return (unsigned short)(v << 8 | v >> 8); }
unsigned long _byteswap_ulong(unsigned long v) { return __builtin_bswap32((unsigned)v); }
unsigned __int64 _byteswap_uint64(unsigned __int64 v) { return __builtin_bswap64(v); }

int SDL_SetError(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_error, sizeof g_error, fmt, ap);
    va_end(ap);
    return -1;
}
int SDL_Error(SDL_errorcode code) { return SDL_SetError(code == SDL_ENOMEM ? "Out of memory" : "SDL error %d", (int)code); }
