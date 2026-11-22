/* Display filters. Everything runs on the GPU through SDL's renderer (no shaders):
 *
 *  - an overlay texture the size of the picture, multiplied over it (SDL_BLENDMODE_MOD):
 *    scanlines lined up with the game's lines, aperture-grille stripes, the LCD grid
 *    and the vignette. It is rebuilt only when the size or the filter changes.
 *  - CRT glow: the picture drawn into a target 1/8 the size and added back on top,
 *    stretched with linear filtering, which blurs it.
 *  - CRT curvature: the picture and the overlay are drawn as a grid of triangles bent
 *    into a barrel shape (SDL_RenderGeometry). */
#include "filter.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

static uint32_t *overlay_px;
static int ov_w, ov_h, ov_sw, ov_sh, ov_mode = -1;
static SDL_Texture *overlay_tex;
static SDL_Renderer *overlay_ren;
static SDL_Texture *glow_tex;
static int glow_w, glow_h;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

const uint32_t *filter_overlay(int w, int h, int src_w, int src_h, int mode) {
    if (overlay_px && w == ov_w && h == ov_h && src_w == ov_sw && src_h == ov_sh && mode == ov_mode) return overlay_px;
    free(overlay_px);
    overlay_px = malloc((size_t)w * h * 4);
    ov_w = w; ov_h = h; ov_sw = src_w; ov_sh = src_h; ov_mode = mode;
    if (overlay_tex) { SDL_DestroyTexture(overlay_tex); overlay_tex = NULL; }

    float line_px = (float)h / src_h, col_px = (float)w / src_w;
    /* Scanlines need a few screen pixels per line; fade them out when there are fewer. */
    float depth = mode == FILTER_CRT ? .62f : .5f;
    depth *= clampf((line_px - 1.5f) / 1.5f, 0, 1);
    /* Per row: the scanline level; per column: the CRT's x term and stripe. Only the CRT's
     * vignette needs a value per pixel (built once per size and filter, but on the console
     * the per-pixel powf took 2.6 s at 1280x720). */
    static const float stripe[3][3] = { { 1, .72f, .72f }, { .72f, 1, .72f }, { .72f, .72f, 1 } };
    float *nx2 = malloc((size_t)w * sizeof *nx2);
    uint8_t *edge_x = malloc((size_t)w);
    float gap = .9f / col_px, gapy = .9f / line_px;
    for (int x = 0; x < w; x++) {
        float nx = (x + .5f) / w * 2 - 1;
        nx2[x] = nx * nx;
        float fx = (x + .5f) / col_px;
        fx -= floorf(fx);
        edge_x[x] = fx < gap;
    }
    bool lcd = mode == FILTER_LCD && col_px >= 2.5f && line_px >= 2.5f;
    for (int y = 0; y < h; y++) {
        float fy = (y + .5f) / line_px;
        fy -= floorf(fy);
        float d = fabsf(fy - .5f) * 2;                  /* 0 at the beam centre, 1 between lines */
        float scan = 1 - depth * powf(d, 2.2f);
        float ny = (y + .5f) / h * 2 - 1, ny2 = ny * ny;
        uint32_t *row = overlay_px + (size_t)y * w;
        if (mode == FILTER_CRT) {
            for (int x = 0; x < w; x++) {
                const float *st = stripe[x % 3];
                float q = nx2[x] + ny2;
                float v = scan * (1 - .22f * q * q);       /* scanline and vignette */
                float r = st[0] * v, g = st[1] * v, b = st[2] * v;
                uint32_t R = (uint32_t)(clampf(r, 0, 1) * 255 + .5f), G = (uint32_t)(clampf(g, 0, 1) * 255 + .5f),
                         B = (uint32_t)(clampf(b, 0, 1) * 255 + .5f);
                row[x] = R << 16 | G << 8 | B;
            }
        } else if (lcd) {
            /* Pixel grid: a thin darker gap around every game pixel. */
            uint32_t m = (uint32_t)(.78f * 255 + .5f), on = 0xFFFFFFu & (m << 16 | m << 8 | m);
            bool edge_row = fy < gapy;
            for (int x = 0; x < w; x++) row[x] = (edge_row || edge_x[x]) ? on : 0xFFFFFF;
        } else {
            float c = mode == FILTER_SCANLINES ? scan : 1;
            uint32_t v = (uint32_t)(clampf(c, 0, 1) * 255 + .5f), px = v << 16 | v << 8 | v;
            for (int x = 0; x < w; x++) row[x] = px;
        }
    }
    free(nx2);
    free(edge_x);
    return overlay_px;
}

/* Barrel-shaped grid: (u, v) in 0..1 to a point in dst. The middle of each edge
 * reaches dst's edge and the corners are pulled in, like a CRT's bulging glass. */
#define GRID_X 40
#define GRID_Y 30
static void warp(float u, float v, SDL_Rect dst, float k, float *x, float *y) {
    float nx = u * 2 - 1, ny = v * 2 - 1;
    float wx = nx * (1 - k * ny * ny), wy = ny * (1 - k * nx * nx);
    *x = dst.x + (wx + 1) * .5f * dst.w;
    *y = dst.y + (wy + 1) * .5f * dst.h;
}

/* Draws tex's sub-rectangle (in texture coordinates 0..1) bent over dst. */
static void draw_curved(SDL_Renderer *ren, SDL_Texture *tex, float u0, float v0, float u1, float v1, SDL_Rect dst,
                        float k, SDL_Color color) {
    static SDL_Vertex verts[(GRID_X + 1) * (GRID_Y + 1)];
    static int idx[GRID_X * GRID_Y * 6];
    for (int j = 0; j <= GRID_Y; j++)
        for (int i = 0; i <= GRID_X; i++) {
            float u = (float)i / GRID_X, v = (float)j / GRID_Y;
            SDL_Vertex *p = &verts[j * (GRID_X + 1) + i];
            warp(u, v, dst, k, &p->position.x, &p->position.y);
            p->tex_coord.x = u0 + (u1 - u0) * u;
            p->tex_coord.y = v0 + (v1 - v0) * v;
            p->color = color;
        }
    int n = 0;
    for (int j = 0; j < GRID_Y; j++)
        for (int i = 0; i < GRID_X; i++) {
            int a = j * (GRID_X + 1) + i, b = a + 1, c = a + GRID_X + 1, d = c + 1;
            idx[n++] = a; idx[n++] = b; idx[n++] = c;
            idx[n++] = b; idx[n++] = d; idx[n++] = c;
        }
    SDL_RenderGeometry(ren, tex, verts, (GRID_X + 1) * (GRID_Y + 1), idx, n);
}

/* Sharp bilinear: the picture is first enlarged by a whole number with hard pixel edges
 * to just above dst's size, then smoothed only over the small remaining step. A plain
 * smooth stretch from, say, 1272x672 to 1920x1080 blurs every edge. */
static SDL_Texture *sharp_tex;
static SDL_Renderer *sharp_ren;
static int sharp_w, sharp_h;

static SDL_Texture *sharpen(SDL_Renderer *ren, SDL_Texture *frame, SDL_Rect *s, SDL_Rect dst) {
    int k = (dst.w + s->w - 1) / s->w, ky = (dst.h + s->h - 1) / s->h;
    if (ky > k) k = ky;
    if (k < 2 || (dst.w % s->w == 0 && dst.h % s->h == 0 && dst.w / s->w == dst.h / s->h)) return NULL;
    int w = s->w * k, h = s->h * k;
    if (w > 8192 || h > 8192) return NULL;
    if (!sharp_tex || sharp_ren != ren || sharp_w != w || sharp_h != h) {
        if (sharp_tex) SDL_DestroyTexture(sharp_tex);
        sharp_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
        if (!sharp_tex) return NULL;
        SDL_SetTextureScaleMode(sharp_tex, SDL_ScaleModeLinear);
        sharp_ren = ren; sharp_w = w; sharp_h = h;
    }
    SDL_ScaleMode old_mode;
    SDL_GetTextureScaleMode(frame, &old_mode);
    SDL_Texture *old_target = SDL_GetRenderTarget(ren);
    SDL_SetRenderTarget(ren, sharp_tex);
    SDL_SetTextureScaleMode(frame, SDL_ScaleModeNearest);
    SDL_SetTextureBlendMode(frame, SDL_BLENDMODE_NONE);
    SDL_RenderCopy(ren, frame, s, NULL);
    SDL_SetTextureScaleMode(frame, old_mode);
    SDL_SetRenderTarget(ren, old_target);
    *s = (SDL_Rect){ 0, 0, w, h };
    return sharp_tex;
}

void filter_draw(SDL_Renderer *ren, SDL_Texture *frame, const SDL_Rect *src, SDL_Rect dst,
                 int src_w, int src_h, int mode) {
    if (dst.w <= 0 || dst.h <= 0) return;
    int tw, th;
    SDL_QueryTexture(frame, NULL, NULL, &tw, &th);
    SDL_Rect s = src ? *src : (SDL_Rect){ 0, 0, tw, th };
    SDL_Texture *sharp = sharpen(ren, frame, &s, dst);
    if (sharp) { frame = sharp; tw = s.w; th = s.h; }
    if (mode <= FILTER_OFF || mode >= FILTER_COUNT) {
        SDL_RenderCopy(ren, frame, &s, &dst);
        return;
    }
    float u0 = (float)s.x / tw, v0 = (float)s.y / th, u1 = (float)(s.x + s.w) / tw, v1 = (float)(s.y + s.h) / th;
    bool crt = mode == FILTER_CRT;
    float k = crt ? .045f : 0;
    SDL_Color white = { 255, 255, 255, 255 };

    /* The picture. */
    SDL_SetTextureBlendMode(frame, SDL_BLENDMODE_NONE);
    if (crt) draw_curved(ren, frame, u0, v0, u1, v1, dst, k, white);
    else SDL_RenderCopy(ren, frame, &s, &dst);

    /* The overlay, multiplied. */
    if (overlay_ren != ren || ov_w != dst.w || ov_h != dst.h || ov_sw != src_w || ov_sh != src_h || ov_mode != mode ||
        !overlay_tex) {
        const uint32_t *px = filter_overlay(dst.w, dst.h, src_w, src_h, mode);
        if (overlay_tex) SDL_DestroyTexture(overlay_tex);
        overlay_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STATIC, dst.w, dst.h);
        SDL_UpdateTexture(overlay_tex, NULL, px, dst.w * 4);
        SDL_SetTextureBlendMode(overlay_tex, SDL_BLENDMODE_MOD);
        overlay_ren = ren;
    }
    if (crt) draw_curved(ren, overlay_tex, 0, 0, 1, 1, dst, k, white);
    else SDL_RenderCopy(ren, overlay_tex, NULL, &dst);

    /* CRT glow: a blurred copy added on top, which also gives back the light the
     * scanlines and the mask take away. */
    if (crt) {
        int gw = dst.w / 8 > 1 ? dst.w / 8 : 1, gh = dst.h / 8 > 1 ? dst.h / 8 : 1;
        if (!glow_tex || glow_w != gw || glow_h != gh) {
            if (glow_tex) SDL_DestroyTexture(glow_tex);
            glow_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, gw, gh);
            SDL_SetTextureScaleMode(glow_tex, SDL_ScaleModeLinear);
            glow_w = gw; glow_h = gh;
        }
        if (glow_tex) {
            SDL_Texture *old = SDL_GetRenderTarget(ren);
            SDL_SetRenderTarget(ren, glow_tex);
            SDL_RenderCopy(ren, frame, &s, NULL);
            SDL_SetRenderTarget(ren, old);
            SDL_SetTextureBlendMode(glow_tex, SDL_BLENDMODE_ADD);
            SDL_Color glow = { 60, 60, 60, 255 };
            draw_curved(ren, glow_tex, 0, 0, 1, 1, dst, k, glow);
        }
    }
}
