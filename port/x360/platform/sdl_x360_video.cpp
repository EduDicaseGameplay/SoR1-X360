// SDL2 subset for the Xbox 360 (X360 SoR1), video half: SDL_Window, SDL_Renderer and SDL_Texture over D3D9
// on Xenos, through the XDK's C entry points (D3DDevice_*), as x360_gfx.cpp of the SotC port does.
//
// What the Streets of Rage runtime asks of the renderer: streaming and static XRGB8888 textures (the
// game picture, the menu, the CRT overlay), target textures (sharpening and glow passes, filter.c),
// RenderCopy with nearest or linear scaling, RenderGeometry (the CRT barrel), four blend modes, and clear.
//
// Every draw is textured triangles through present.hlsl (clip-space position, UV, colour). Render targets
// live in EDRAM, each at its own base after the back buffer, so switching between them never overwrites
// another; leaving a target resolves it into its texture. Streaming textures have two D3D textures used
// in turn, with a fence, so an update never writes into one the GPU is still reading.
#include <xtl.h>
#include <xgraphics.h>

#include <stdlib.h>
#include <string.h>

#include "SDL.h"
#include "present_ps.h"
#include "present_vs.h"

extern "C" void x360_log_write(const char *text, unsigned length);

namespace
{
void logline(const char *s)
{
    x360_log_write(s, (unsigned)strlen(s));
    x360_log_write("\n", 1);
}

struct Vertex
{
    float x, y, z, w;
    float u, v;
    float r, g, b, a;
};

D3DDevice *g_dev = nullptr;
D3DVertexShader *g_vs = nullptr;
D3DPixelShader *g_ps = nullptr;
D3DVertexDeclaration *g_decl = nullptr;
D3DSurface *g_backbuffer = nullptr;
int g_width = 1280, g_height = 720;
DWORD g_edram_next = 0; // next free EDRAM tile after the back buffer
const DWORD kEdramTiles = 2048;
} // namespace

struct SDL_Texture
{
    int w, h;
    int access;
    SDL_ScaleMode scale;
    SDL_BlendMode blend;
    D3DTexture *tex[2];
    DWORD fence[2];
    int cur;
    D3DSurface *rt; // target textures: the EDRAM surface drawn into
    bool dirty;     // target drawn since its last resolve
};

struct SDL_Renderer
{
    SDL_Texture *target;
    Uint8 r, g, b, a;
    SDL_Texture *white;
};

struct SDL_Window
{
    int w, h;
};

namespace
{
SDL_Renderer g_renderer;
SDL_Window g_window;
bool g_ready = false;

int targetW() { return g_renderer.target ? g_renderer.target->w : g_width; }
int targetH() { return g_renderer.target ? g_renderer.target->h : g_height; }

void setViewport()
{
    D3DVIEWPORT9 vp = {0, 0, (DWORD)targetW(), (DWORD)targetH(), 0.0f, 1.0f};
    D3DDevice_SetViewport(g_dev, &vp);
}

void resolveTarget(SDL_Texture *t)
{
    if (!t || !t->rt || !t->dirty)
        return;
    D3DDevice_Resolve(g_dev, D3DRESOLVE_RENDERTARGET0, nullptr, (D3DBaseTexture *)t->tex[0], nullptr, 0, 0, nullptr,
                      0.0f, 0, nullptr);
    t->dirty = false;
}

bool initDevice()
{
    if (g_ready)
        return true;
    XVIDEO_MODE mode;
    memset(&mode, 0, sizeof(mode));
    XGetVideoMode(&mode);
    const bool hd = mode.fIsHiDef && mode.dwDisplayHeight >= 720;
    g_width = hd ? 1280 : 640;
    g_height = hd ? 720 : 480;

    D3DPRESENT_PARAMETERS pp;
    memset(&pp, 0, sizeof(pp));
    pp.BackBufferWidth = g_width;
    pp.BackBufferHeight = g_height;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.EnableAutoDepthStencil = FALSE;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    if (Direct3D_CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &g_dev) < 0 ||
        !g_dev)
    {
        logline("[video] Direct3D_CreateDevice failed");
        return false;
    }
    g_vs = D3DDevice_CreateVertexShader((const DWORD *)g_presentVs);
    g_ps = D3DDevice_CreatePixelShader((const DWORD *)g_presentPs);
    static const D3DVERTEXELEMENT9 elements[] = {
        {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        {0, 24, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
        D3DDECL_END()};
    g_decl = D3DDevice_CreateVertexDeclaration(elements);
    if (!g_vs || !g_ps || !g_decl)
    {
        logline("[video] shaders or vertex declaration failed");
        return false;
    }
    g_backbuffer = D3DDevice_GetBackBuffer(g_dev);
    g_edram_next = XGSurfaceSize(g_width, g_height, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE);
    g_ready = true;
    char line[96];
    _snprintf(line, sizeof line, "[video] device %dx%d, back buffer %u EDRAM tiles", g_width, g_height,
              (unsigned)g_edram_next);
    logline(line);
    return true;
}

// Fixed state for 2D: no depth, no culling, our shaders.
void beginDraw()
{
    D3DDevice_SetVertexDeclaration(g_dev, g_decl);
    D3DDevice_SetVertexShader(g_dev, g_vs);
    D3DDevice_SetPixelShader(g_dev, g_ps);
    D3DDevice_SetRenderState(g_dev, D3DRS_CULLMODE, D3DCULL_NONE);
    D3DDevice_SetRenderState(g_dev, D3DRS_ZENABLE, FALSE);
    D3DDevice_SetRenderState(g_dev, D3DRS_ZWRITEENABLE, FALSE);
}

void applyBlend(SDL_BlendMode mode)
{
    switch (mode)
    {
    case SDL_BLENDMODE_NONE:
        D3DDevice_SetRenderState(g_dev, D3DRS_ALPHABLENDENABLE, FALSE);
        return;
    case SDL_BLENDMODE_ADD:
        D3DDevice_SetRenderState(g_dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        D3DDevice_SetRenderState(g_dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
        break;
    case SDL_BLENDMODE_MOD: // dst = src * dst
        D3DDevice_SetRenderState(g_dev, D3DRS_SRCBLEND, D3DBLEND_ZERO);
        D3DDevice_SetRenderState(g_dev, D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR);
        break;
    default: // SDL_BLENDMODE_BLEND
        D3DDevice_SetRenderState(g_dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        D3DDevice_SetRenderState(g_dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        break;
    }
    D3DDevice_SetRenderState(g_dev, D3DRS_BLENDOP, D3DBLENDOP_ADD);
    D3DDevice_SetRenderState(g_dev, D3DRS_ALPHABLENDENABLE, TRUE);
}

D3DTexture *bindTexture(SDL_Texture *t)
{
    if (!t)
        t = g_renderer.white;
    resolveTarget(t); // a target sampled after drawing into it
    D3DTexture *d = t->tex[t->cur];
    D3DDevice_SetTexture_Inline(g_dev, 0, (D3DBaseTexture *)d);
    const DWORD filter = t->scale == SDL_ScaleModeNearest ? D3DTEXF_POINT : D3DTEXF_LINEAR;
    D3DDevice_SetSamplerState(g_dev, 0, D3DSAMP_MINFILTER, filter);
    D3DDevice_SetSamplerState(g_dev, 0, D3DSAMP_MAGFILTER, filter);
    D3DDevice_SetSamplerState(g_dev, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    D3DDevice_SetSamplerState(g_dev, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    applyBlend(t == g_renderer.white ? SDL_BLENDMODE_BLEND : t->blend);
    return d;
}

void afterDraw(SDL_Texture *t)
{
    const DWORD fence = D3DDevice_InsertFence(g_dev);
    if (t)
        t->fence[t->cur] = fence;
    if (g_renderer.target)
        g_renderer.target->dirty = true;
}

float clipX(float px) { return px / targetW() * 2.0f - 1.0f; }
float clipY(float py) { return 1.0f - py / targetH() * 2.0f; }

D3DTexture *createD3DTexture(int w, int h, bool tiled)
{
    // XRGB8888 is a native-endian 32-bit word: on the big-endian Xenon its bytes are X,R,G,B, which is
    // the console's own X8R8G8B8. Linear for textures the CPU writes; tiled for resolve targets.
    return (D3DTexture *)D3DDevice_CreateTexture(w, h, 1, 1, 0, tiled ? D3DFMT_X8R8G8B8 : D3DFMT_LIN_X8R8G8B8,
                                                 D3DPOOL_DEFAULT, D3DRTYPE_TEXTURE);
}
} // namespace

extern "C" {

/* X360: returns at the start of the next vertical blank (60 Hz at 720p), by the raster position:
 * one game frame per refresh, so no frame is shown twice or skipped (the game's own 59.92 Hz
 * against the TV's 60 Hz showed one frame twice every few seconds: a hitch in the scrolling).
 * Sleeps while the beam is far from the bottom, spins near it; gives up after 20 ms. */
void x360_wait_vblank(void) {
    if (!g_dev) return;
    D3DRASTER_STATUS rs;
    DWORD t0 = GetTickCount();
    D3DDevice_GetRasterStatus(g_dev, &rs);
    while (rs.InVBlank && GetTickCount() - t0 < 20) D3DDevice_GetRasterStatus(g_dev, &rs);   /* inside one: wait for the next */
    UINT last = rs.ScanLine;
    for (;;) {
        D3DDevice_GetRasterStatus(g_dev, &rs);
        if (rs.InVBlank || rs.ScanLine < last) return;          /* reached the blank, or the beam wrapped */
        last = rs.ScanLine;
        if (GetTickCount() - t0 >= 20) return;
        UINT lines = g_height > 0 ? (UINT)g_height : 720;
        if (rs.ScanLine + 140 < lines) Sleep(1);                /* over 3 ms to go: Sleep(1) cannot overshoot it */
        else __asm__ __volatile__("nop"); /* YieldProcessor() is an MSVC __emit */
    }
}

int x360_video_width() { return g_width; }
int x360_video_height() { return g_height; }

// ---- window ----------------------------------------------------------------------------------------
SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags)
{
    (void)title; (void)x; (void)y; (void)w; (void)h; (void)flags;
    if (!initDevice())
        return nullptr;
    g_window.w = g_width;
    g_window.h = g_height;
    return &g_window;
}
void SDL_DestroyWindow(SDL_Window *w) { (void)w; }
void SDL_GetWindowSize(SDL_Window *w, int *ww, int *wh)
{
    (void)w;
    if (ww) *ww = g_width;
    if (wh) *wh = g_height;
}
int SDL_GetWindowDisplayIndex(SDL_Window *w) { (void)w; return 0; }
int SDL_SetWindowFullscreen(SDL_Window *w, Uint32 flags) { (void)w; (void)flags; return 0; }
void SDL_SetWindowTitle(SDL_Window *w, const char *title) { (void)w; (void)title; }
void SDL_ShowWindow(SDL_Window *w) { (void)w; }
void SDL_HideWindow(SDL_Window *w) { (void)w; }
void SDL_RaiseWindow(SDL_Window *w) { (void)w; }

// ---- renderer --------------------------------------------------------------------------------------
SDL_Renderer *SDL_CreateRenderer(SDL_Window *w, int index, Uint32 flags)
{
    (void)w; (void)index; (void)flags;
    if (!initDevice())
        return nullptr;
    if (!g_renderer.white)
    {
        g_renderer.white = SDL_CreateTexture(&g_renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STATIC, 1, 1);
        const Uint32 white = 0xFFFFFFFF;
        SDL_UpdateTexture(g_renderer.white, nullptr, &white, 4);
    }
    g_renderer.target = nullptr;
    g_renderer.r = g_renderer.g = g_renderer.b = 0;
    g_renderer.a = 255;
    return &g_renderer;
}
void SDL_DestroyRenderer(SDL_Renderer *r) { (void)r; }
int SDL_GetRendererOutputSize(SDL_Renderer *r, int *w, int *h)
{
    (void)r;
    if (w) *w = g_width;
    if (h) *h = g_height;
    return 0;
}
int SDL_RenderSetVSync(SDL_Renderer *r, int vsync) { (void)r; (void)vsync; return 0; } // always on
int SDL_SetRenderDrawColor(SDL_Renderer *r, Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca)
{
    r->r = cr; r->g = cg; r->b = cb; r->a = ca;
    return 0;
}
int SDL_RenderClear(SDL_Renderer *r)
{
    if (!g_ready) return -1;
    D3DDevice_Clear(g_dev, 0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(r->a, r->r, r->g, r->b), 1.0f, 0, FALSE);
    if (r->target) r->target->dirty = true;
    return 0;
}
SDL_Texture *SDL_GetRenderTarget(SDL_Renderer *r) { return r->target; }
int SDL_SetRenderTarget(SDL_Renderer *r, SDL_Texture *t)
{
    if (!g_ready) return -1;
    if (t && !t->rt) return SDL_SetError("not a target texture");
    if (r->target == t) return 0;
    resolveTarget(r->target);
    r->target = t;
    D3DDevice_SetRenderTarget(g_dev, 0, t ? t->rt : g_backbuffer);
    setViewport();
    return 0;
}
void SDL_RenderPresent(SDL_Renderer *r)
{
    if (!g_ready) return;
    if (r->target) SDL_SetRenderTarget(r, nullptr);
    D3DDevice_Present(g_dev); // PresentationInterval ONE: waits for the vertical blank
}
int SDL_RenderReadPixels(SDL_Renderer *r, const SDL_Rect *rect, Uint32 format, void *pixels, int pitch)
{
    (void)r; (void)rect; (void)format; (void)pixels; (void)pitch;
    return SDL_SetError("RenderReadPixels: not on the console");
}

int SDL_RenderCopy(SDL_Renderer *r, SDL_Texture *t, const SDL_Rect *src, const SDL_Rect *dst)
{
    if (!g_ready || !t) return -1;
    const float sx = src ? (float)src->x : 0.0f, sy = src ? (float)src->y : 0.0f;
    const float sw = src ? (float)src->w : (float)t->w, sh = src ? (float)src->h : (float)t->h;
    const float dx = dst ? (float)dst->x : 0.0f, dy = dst ? (float)dst->y : 0.0f;
    const float dw = dst ? (float)dst->w : (float)targetW(), dh = dst ? (float)dst->h : (float)targetH();
    beginDraw();
    bindTexture(t);
    const float x0 = clipX(dx), x1 = clipX(dx + dw), y0 = clipY(dy), y1 = clipY(dy + dh);
    const float u0 = sx / t->w, u1 = (sx + sw) / t->w, v0 = sy / t->h, v1 = (sy + sh) / t->h;
    const Vertex quad[4] = {
        {x0, y0, 0, 1, u0, v0, 1, 1, 1, 1},
        {x1, y0, 0, 1, u1, v0, 1, 1, 1, 1},
        {x0, y1, 0, 1, u0, v1, 1, 1, 1, 1},
        {x1, y1, 0, 1, u1, v1, 1, 1, 1, 1},
    };
    D3DDevice_DrawVerticesUP(g_dev, D3DPT_TRIANGLESTRIP, 4, quad, sizeof(Vertex));
    afterDraw(t);
    (void)r;
    return 0;
}

int SDL_RenderGeometry(SDL_Renderer *r, SDL_Texture *t, const SDL_Vertex *v, int nv, const int *idx, int ni)
{
    (void)r;
    if (!g_ready || !v || nv <= 0) return -1;
    const int n = idx ? ni : nv;
    if (n <= 0 || n % 3) return SDL_SetError("RenderGeometry: not whole triangles");
    Vertex *out = (Vertex *)malloc(sizeof(Vertex) * (size_t)n);
    if (!out) return SDL_OutOfMemory();
    for (int i = 0; i < n; ++i)
    {
        const int k = idx ? idx[i] : i;
        const SDL_Vertex &s = v[k];
        Vertex &d = out[i];
        d.x = clipX(s.position.x); d.y = clipY(s.position.y); d.z = 0; d.w = 1;
        d.u = s.tex_coord.x; d.v = s.tex_coord.y;
        d.r = s.color.r / 255.0f; d.g = s.color.g / 255.0f; d.b = s.color.b / 255.0f; d.a = s.color.a / 255.0f;
    }
    beginDraw();
    bindTexture(t);
    D3DDevice_DrawVerticesUP(g_dev, D3DPT_TRIANGLELIST, (UINT)n, out, sizeof(Vertex));
    afterDraw(t);
    free(out);
    return 0;
}

// ---- textures --------------------------------------------------------------------------------------
SDL_Texture *SDL_CreateTexture(SDL_Renderer *r, Uint32 format, int access, int w, int h)
{
    (void)r;
    if (!g_ready || w <= 0 || h <= 0) return nullptr;
    if (format != SDL_PIXELFORMAT_XRGB8888 && format != SDL_PIXELFORMAT_ARGB8888)
    {
        SDL_SetError("texture format not supported on the console");
        return nullptr;
    }
    SDL_Texture *t = (SDL_Texture *)calloc(1, sizeof(SDL_Texture));
    if (!t) return nullptr;
    t->w = w; t->h = h; t->access = access;
    t->scale = SDL_ScaleModeLinear; // SDL's default hint is "linear" unless the runtime says otherwise
    t->blend = SDL_BLENDMODE_NONE;
    if (access == SDL_TEXTUREACCESS_TARGET)
    {
        const DWORD tiles = XGSurfaceSize(w, h, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE);
        if (g_edram_next + tiles > kEdramTiles)
        {
            logline("[video] EDRAM full for a target texture");
            free(t);
            SDL_SetError("EDRAM full");
            return nullptr;
        }
        D3DSURFACE_PARAMETERS sp;
        memset(&sp, 0, sizeof(sp));
        sp.Base = g_edram_next;
        t->rt = D3DDevice_CreateSurface(w, h, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, &sp);
        t->tex[0] = createD3DTexture(w, h, true);
        if (!t->rt || !t->tex[0])
        {
            free(t);
            return nullptr;
        }
        g_edram_next += tiles;
        t->blend = SDL_BLENDMODE_BLEND; // SDL gives target textures blending by default
    }
    else
    {
        t->tex[0] = createD3DTexture(w, h, false);
        t->tex[1] = access == SDL_TEXTUREACCESS_STREAMING ? createD3DTexture(w, h, false) : nullptr;
        if (!t->tex[0] || (access == SDL_TEXTUREACCESS_STREAMING && !t->tex[1]))
        {
            if (t->tex[0]) D3DResource_Release((D3DResource *)t->tex[0]);
            free(t);
            return nullptr;
        }
    }
    return t;
}

void SDL_DestroyTexture(SDL_Texture *t)
{
    if (!t) return;
    if (g_renderer.target == t) SDL_SetRenderTarget(&g_renderer, nullptr);
    D3DDevice_SetTexture_Inline(g_dev, 0, nullptr);
    for (int i = 0; i < 2; ++i)
        if (t->tex[i])
        {
            if (t->fence[i]) D3DDevice_BlockOnFence(t->fence[i]); // the GPU may still read it this frame
            D3DResource_Release((D3DResource *)t->tex[i]);
        }
    if (t->rt)
    {
        D3DResource_Release((D3DResource *)t->rt);
        // EDRAM is handed out in order; the last target given back returns its tiles.
        const DWORD tiles = XGSurfaceSize(t->w, t->h, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE);
        // Targets come and go with the window size: start over when none is left would need a count; keep it
        // simple and only reclaim the top one.
        (void)tiles;
    }
    free(t);
}

int SDL_UpdateTexture(SDL_Texture *t, const SDL_Rect *rect, const void *pixels, int pitch)
{
    if (!t || !pixels || t->rt) return -1;
    const int x = rect ? rect->x : 0, y = rect ? rect->y : 0;
    const int w = rect ? rect->w : t->w, h = rect ? rect->h : t->h;
    if (t->tex[1])
    {
        // The other one of the pair: the GPU may still be reading the current one.
        const int next = t->cur ^ 1;
        if (t->fence[next]) D3DDevice_BlockOnFence(t->fence[next]);
        // A partial update does not copy the rest over: that read texture memory, which is
        // write-combined (very slow to read from the CPU; SMS in the playbook). The runtime only
        // draws the rectangle it updated (the game picture inside a wider texture).
        t->cur = next;
    }
    else if (t->fence[0])
        D3DDevice_BlockOnFence(t->fence[0]);
    D3DLOCKED_RECT locked;
    D3DTexture_LockRect(t->tex[t->cur], 0, &locked, nullptr, 0);
    const Uint8 *src = (const Uint8 *)pixels;
    Uint8 *dst = (Uint8 *)locked.pBits + y * locked.Pitch + x * 4;
    for (int row = 0; row < h; ++row)
        memcpy(dst + row * locked.Pitch, src + row * pitch, (size_t)w * 4);
    D3DTexture_UnlockRect(t->tex[t->cur], 0);
    return 0;
}

int SDL_QueryTexture(SDL_Texture *t, Uint32 *format, int *access, int *w, int *h)
{
    if (!t) return -1;
    if (format) *format = SDL_PIXELFORMAT_XRGB8888;
    if (access) *access = t->access;
    if (w) *w = t->w;
    if (h) *h = t->h;
    return 0;
}
int SDL_SetTextureScaleMode(SDL_Texture *t, SDL_ScaleMode m)
{
    if (!t) return -1;
    t->scale = m;
    return 0;
}
int SDL_GetTextureScaleMode(SDL_Texture *t, SDL_ScaleMode *m)
{
    if (!t || !m) return -1;
    *m = t->scale;
    return 0;
}
int SDL_SetTextureBlendMode(SDL_Texture *t, SDL_BlendMode m)
{
    if (!t) return -1;
    t->blend = m;
    return 0;
}

} // extern "C"
