// Audio half of the Xbox 360 host backend (x360_backend.h): XAudio2 through its C interface (lpVtbl), so no C++
// object crosses between clang and Microsoft code. C, not C++: xaudio2.h only gives the C interface to C (in C++ it
// puts default arguments into the vtable's function pointers).
//
// The PC runtime's music/SPU2 output is a pull stream: raylib calls its callback for N frames of interleaved int16
// from the audio thread (ps2_audio.cpp, 48 kHz stereo, 512-frame buffers). Here a thread keeps four buffers queued
// on one source voice and refills them through the same callback. XAudio2 on the console takes big-endian PCM,
// which is what native int16 is here; the one-shot sounds come from ps2_audio.cpp's WAV builder, whose samples are
// native too (only its header is little-endian).
#include <xtl.h>
#include <xaudio2.h>

#include <stdlib.h>
#include <string.h>

#include "x360_backend.h"

static IXAudio2 *g_xa = NULL;
static IXAudio2MasteringVoice *g_master = NULL;

#define kStreamBuffers 4

typedef struct Stream
{
    IXAudio2SourceVoice *voice;
    void (*callback)(void *, unsigned int);
    int channels;
    int frames;
    short *buffers[kStreamBuffers];
    int next;
    volatile LONG running;
    HANDLE thread;
} Stream;

typedef struct Sound
{
    IXAudio2SourceVoice *voice;
    short *pcm;
    unsigned int frames;
    int channels;
} Sound;

static WAVEFORMATEX pcmFormat(int rate, int channels)
{
    WAVEFORMATEX f;
    memset(&f, 0, sizeof(f));
    f.wFormatTag = WAVE_FORMAT_PCM;
    f.nChannels = (WORD)channels;
    f.nSamplesPerSec = (DWORD)rate;
    f.wBitsPerSample = 16;
    f.nBlockAlign = (WORD)(channels * 2);
    f.nAvgBytesPerSec = f.nSamplesPerSec * f.nBlockAlign;
    return f;
}

static DWORD WINAPI StreamThread(LPVOID p)
{
    Stream *s = (Stream *)p;
    while (s->running)
    {
        XAUDIO2_VOICE_STATE st;
        IXAudio2SourceVoice_GetState(s->voice, &st, 0);
        while (st.BuffersQueued < (UINT32)kStreamBuffers && s->running)
        {
            short *buf = s->buffers[s->next];
            if (s->callback)
                s->callback(buf, (unsigned int)s->frames);
            else
                memset(buf, 0, (size_t)s->frames * s->channels * 2);
            XAUDIO2_BUFFER b;
            memset(&b, 0, sizeof(b));
            b.AudioBytes = (UINT32)(s->frames * s->channels * 2);
            b.pAudioData = (const BYTE *)buf;
            IXAudio2SourceVoice_SubmitSourceBuffer(s->voice, &b, NULL);
            s->next = (s->next + 1) % kStreamBuffers;
            ++st.BuffersQueued;
        }
        Sleep(2);
    }
    return 0;
}

int x360snd_init()
{
    if (g_xa)
        return 1;
    if (XAudio2Create(&g_xa, 0, XboxThread5) < 0 || !g_xa)
        return 0;
    if (IXAudio2_CreateMasteringVoice(g_xa, &g_master, XAUDIO2_DEFAULT_CHANNELS, XAUDIO2_DEFAULT_SAMPLERATE, 0, 0,
                                      NULL) < 0)
        return 0;
    return 1;
}

void x360snd_shutdown()
{
    if (g_master)
        IXAudio2MasteringVoice_DestroyVoice(g_master);
    if (g_xa)
        IXAudio2_Release(g_xa);
    g_master = NULL;
    g_xa = NULL;
}

void x360snd_master_volume(float v)
{
    if (g_master)
        IXAudio2MasteringVoice_SetVolume(g_master, v, XAUDIO2_COMMIT_NOW);
}

void *x360snd_stream_create(int rate, int channels, int frames)
{
    if (!g_xa)
        return NULL;
    Stream *s = (Stream *)calloc(1, sizeof(Stream));
    s->channels = channels;
    s->frames = frames > 0 ? frames : 512;
    WAVEFORMATEX f = pcmFormat(rate, channels);
    if (IXAudio2_CreateSourceVoice(g_xa, &s->voice, &f, 0, XAUDIO2_DEFAULT_FREQ_RATIO, NULL, NULL, NULL) < 0)
    {
        free(s);
        return NULL;
    }
    for (int i = 0; i < kStreamBuffers; ++i)
    {
        s->buffers[i] = (short *)malloc((size_t)s->frames * channels * 2);
        memset(s->buffers[i], 0, (size_t)s->frames * channels * 2);
    }
    return s;
}

void x360snd_stream_callback(void *stream, void (*cb)(void *, unsigned int))
{
    Stream *s = (Stream *)stream;
    if (s)
        s->callback = cb;
}

void x360snd_stream_play(void *stream)
{
    Stream *s = (Stream *)stream;
    if (!s || s->running)
        return;
    s->running = 1;
    s->thread = CreateThread(NULL, 64 * 1024, StreamThread, s, CREATE_SUSPENDED, NULL);
    XSetThreadProcessor(s->thread, 4); // XAudio2 runs on hardware thread 5 (XboxThread5); feed from core 2
    ResumeThread(s->thread);
    IXAudio2SourceVoice_Start(s->voice, 0, XAUDIO2_COMMIT_NOW);
}

void x360snd_stream_stop(void *stream)
{
    Stream *s = (Stream *)stream;
    if (!s || !s->running)
        return;
    InterlockedExchange(&s->running, 0);
    WaitForSingleObject(s->thread, INFINITE);
    CloseHandle(s->thread);
    s->thread = NULL;
    IXAudio2SourceVoice_Stop(s->voice, 0, XAUDIO2_COMMIT_NOW);
    IXAudio2SourceVoice_FlushSourceBuffers(s->voice);
}

void x360snd_stream_destroy(void *stream)
{
    Stream *s = (Stream *)stream;
    if (!s)
        return;
    x360snd_stream_stop(s);
    IXAudio2SourceVoice_DestroyVoice(s->voice);
    for (int i = 0; i < kStreamBuffers; ++i)
        free(s->buffers[i]);
    free(s);
}

void *x360snd_sound_create(const int16_t *pcm, unsigned int frames, int channels, int rate)
{
    if (!g_xa || !pcm || !frames || channels <= 0)
        return NULL;
    Sound *s = (Sound *)calloc(1, sizeof(Sound));
    s->frames = frames;
    s->channels = channels;
    s->pcm = (short *)malloc((size_t)frames * channels * 2);
    memcpy(s->pcm, pcm, (size_t)frames * channels * 2);
    WAVEFORMATEX f = pcmFormat(rate, channels);
    // Pitch up to 4x (raylib SetSoundPitch); the PS2 SPU2 rarely goes above 2x.
    if (IXAudio2_CreateSourceVoice(g_xa, &s->voice, &f, 0, 4.0f, NULL, NULL, NULL) < 0)
    {
        free(s->pcm);
        free(s);
        return NULL;
    }
    return s;
}

void x360snd_sound_play(void *sound)
{
    Sound *s = (Sound *)sound;
    if (!s)
        return;
    IXAudio2SourceVoice_Stop(s->voice, 0, XAUDIO2_COMMIT_NOW);
    IXAudio2SourceVoice_FlushSourceBuffers(s->voice);
    XAUDIO2_BUFFER b;
    memset(&b, 0, sizeof(b));
    b.AudioBytes = (UINT32)(s->frames * s->channels * 2);
    b.pAudioData = (const BYTE *)s->pcm;
    b.Flags = XAUDIO2_END_OF_STREAM;
    IXAudio2SourceVoice_SubmitSourceBuffer(s->voice, &b, NULL);
    IXAudio2SourceVoice_Start(s->voice, 0, XAUDIO2_COMMIT_NOW);
}

void x360snd_sound_stop(void *sound)
{
    Sound *s = (Sound *)sound;
    if (!s)
        return;
    IXAudio2SourceVoice_Stop(s->voice, 0, XAUDIO2_COMMIT_NOW);
    IXAudio2SourceVoice_FlushSourceBuffers(s->voice);
}

int x360snd_sound_playing(void *sound)
{
    Sound *s = (Sound *)sound;
    if (!s)
        return 0;
    XAUDIO2_VOICE_STATE st;
    IXAudio2SourceVoice_GetState(s->voice, &st, 0);
    return st.BuffersQueued > 0;
}

void x360snd_sound_volume(void *sound, float v)
{
    Sound *s = (Sound *)sound;
    if (s)
        IXAudio2SourceVoice_SetVolume(s->voice, v, XAUDIO2_COMMIT_NOW);
}

void x360snd_sound_pitch(void *sound, float p)
{
    Sound *s = (Sound *)sound;
    if (s)
        IXAudio2SourceVoice_SetFrequencyRatio(s->voice, p < 0.0005f ? 0.0005f : (p > 4.0f ? 4.0f : p),
                                              XAUDIO2_COMMIT_NOW);
}

void x360snd_sound_destroy(void *sound)
{
    Sound *s = (Sound *)sound;
    if (!s)
        return;
    IXAudio2SourceVoice_DestroyVoice(s->voice);
    free(s->pcm);
    free(s);
}
