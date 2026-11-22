// Host log of the Xbox 360 build: every line goes to game:\sor1_log.txt (next to
// default.xex), opened on the first line and flushed per line so the last line survives a crash. Lines from
// several threads are serialized. Used by std::cout/cerr (libcxx_extra/iostream_x360.cpp) and stderr users.
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>

namespace
{
CRITICAL_SECTION g_cs;
LONG g_csReady = 0;
HANDLE g_file = INVALID_HANDLE_VALUE;
bool g_opened = false;

void ensureLock()
{
    if (InterlockedCompareExchange(&g_csReady, 1, 0) == 0)
    {
        InitializeCriticalSection(&g_cs);
        InterlockedExchange(&g_csReady, 2);
    }
    while (g_csReady != 2)
        SwitchToThread();
}
} // namespace

extern "C" void x360_log_write(const char *text, unsigned length)
{
    if (!text || !length)
        return;
    ensureLock();
    EnterCriticalSection(&g_cs);
    if (!g_opened)
    {
        g_opened = true;
        g_file = CreateFileA("game:\\sor1_log.txt", GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    }
    if (g_file != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteFile(g_file, text, length, &written, nullptr);
        FlushFileBuffers(g_file); // the last line must survive a crash or a killed emulator (Xenia buffers writes)
    }
    // No OutputDebugStringA: in Xenia it hung the calling thread after a few lines (2026-11-15, debug log shows the
    // call entering and never returning). The file is the log; a debugger can tail it.
    LeaveCriticalSection(&g_cs);
}

// Crash logger: an exception nobody handles (access violation, illegal instruction, abort) used to send the console
// back to the dashboard with nothing in the log. The filter writes the exception, the PowerPC registers and the top
// of the stack (return addresses to look up in the link map), then lets the title end as before.
namespace
{
void logf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    const int n = _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    if (n > 0)
        x360_log_write(buf, (unsigned)n);
}

LONG WINAPI crashFilter(EXCEPTION_POINTERS *info)
{
    static LONG s_inside = 0;
    if (InterlockedExchange(&s_inside, 1) != 0)
        return EXCEPTION_CONTINUE_SEARCH;
    const EXCEPTION_RECORD *er = info ? info->ExceptionRecord : nullptr;
    const CONTEXT *c = info ? info->ContextRecord : nullptr;
    logf("\n[CRASH] thread %u exception 0x%08X at 0x%08X\n", (unsigned)GetCurrentThreadId(),
         er ? (unsigned)er->ExceptionCode : 0u, er ? (unsigned)(UINT_PTR)er->ExceptionAddress : 0u);
    if (er && er->NumberParameters >= 2)
        logf("[CRASH] info %s address 0x%08X\n", er->ExceptionInformation[0] ? "write" : "read",
             (unsigned)er->ExceptionInformation[1]);
    if (c)
    {
        logf("[CRASH] iar 0x%08X lr 0x%08X ctr 0x%08X cr 0x%08X\n", (unsigned)c->Iar, (unsigned)c->Lr,
             (unsigned)c->Ctr, (unsigned)c->Cr);
        const ULONGLONG *gpr = &c->Gpr0; // Gpr0..Gpr31 are consecutive
        for (int r = 0; r < 32; r += 4)
            // 32-bit halves: 64-bit varargs into the XDK's _vsnprintf are not trusted (MSVC/clang ABI mismatch).
            logf("[CRASH] r%-2d %08X%08X %08X%08X %08X%08X %08X%08X\n", r, (unsigned)(gpr[r] >> 32), (unsigned)gpr[r],
                 (unsigned)(gpr[r + 1] >> 32), (unsigned)gpr[r + 1], (unsigned)(gpr[r + 2] >> 32), (unsigned)gpr[r + 2],
                 (unsigned)(gpr[r + 3] >> 32), (unsigned)gpr[r + 3]);
        // Words on the stack that look like code addresses (image base 0x82000000): the call chain.
        const DWORD *sp = (const DWORD *)(UINT_PTR)(DWORD)gpr[1];
        if (sp)
        {
            logf("[CRASH] stack code addresses:");
            int found = 0;
            for (int i = 0; i < 768 && found < 40; ++i) // 3 KB: well inside any thread stack
            {
                const DWORD w = sp[i];
                if (w >= 0x82000000u && w < 0x8C000000u)
                {
                    logf(" %08X", (unsigned)w);
                    ++found;
                }
            }
            logf("\n");
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
} // namespace

extern "C" void x360_install_crash_logger()
{
    SetUnhandledExceptionFilter(crashFilter);
}
