// Xbox 360 host backend: the console side of the raylib subset the PC runtime uses (raylib_x360.cpp).
// Plain C interface so the two halves never see each other's headers: raylib.h and the XDK headers both declare
// DrawText, PlaySound, CloseWindow, Rectangle, ...
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- video (x360_gfx.cpp: D3D9 on Xenos) ----------------------------------------------------------------------
int x360gfx_init(int *width, int *height); // creates the device at the console's video mode; 0 on failure
int x360gfx_ready();
void x360gfx_begin(const float clear_rgba[4]);
void x360gfx_end(); // presents (waits for vertical blank)
void *x360gfx_texture_create(int width, int height);
// Pixels are R,G,B,A bytes, rows of width*4 bytes (raylib PIXELFORMAT_UNCOMPRESSED_R8G8B8A8).
void x360gfx_texture_update(void *texture, const void *rgba, int width, int height);
void x360gfx_texture_filter(void *texture, int linear);
void x360gfx_texture_destroy(void *texture);
// Draws texture's source rectangle (texels) to a screen rectangle (pixels), tinted. texture 0 = solid colour.
void x360gfx_draw(void *texture, float sx, float sy, float sw, float sh, int tex_w, int tex_h, float dx, float dy,
                  float dw, float dh, const float rgba[4]);

// ---- input (x360_pad.cpp: XInput) -----------------------------------------------------------------------------
void x360pad_poll();
int x360pad_connected(int pad);
// raylib numbering: GAMEPAD_BUTTON_* (1..17) and GAMEPAD_AXIS_* (0..5).
int x360pad_button(int pad, int raylib_button);
float x360pad_axis(int pad, int raylib_axis);
int x360pad_quit_combo(); // Back + Start held for 2 s on pad 0 (KI, BRE)

// ---- audio (x360_snd.cpp: XAudio2) ----------------------------------------------------------------------------
int x360snd_init(); // 0 on failure
void x360snd_shutdown();
void x360snd_master_volume(float volume);
// Pull stream: callback(buffer, frames) fills interleaved native int16, called from the audio thread.
void *x360snd_stream_create(int sample_rate, int channels, int buffer_frames);
void x360snd_stream_callback(void *stream, void (*callback)(void *buffer, unsigned int frames));
void x360snd_stream_play(void *stream);
void x360snd_stream_stop(void *stream);
void x360snd_stream_destroy(void *stream);
// One-shot sound from native int16 PCM (copied).
void *x360snd_sound_create(const int16_t *pcm, unsigned int frames, int channels, int sample_rate);
void x360snd_sound_play(void *sound);
void x360snd_sound_stop(void *sound);
int x360snd_sound_playing(void *sound);
void x360snd_sound_volume(void *sound, float volume);
void x360snd_sound_pitch(void *sound, float pitch);
void x360snd_sound_destroy(void *sound);

// ---- misc -----------------------------------------------------------------------------------------------------
double x360_time_seconds(); // monotonic
void x360_sleep_seconds(double seconds);

#ifdef __cplusplus
}
#endif
