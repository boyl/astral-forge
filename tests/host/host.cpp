/* aamod test host - an offline stand-in for the game executable.
 *
 * It statically imports winmm.dll exactly like Astral Ascent.exe does (well,
 * a subset of the same 20 entry points) and reports whether the calls actually
 * reach the real implementation through the shim's forwarders. It also waits
 * for the aamod core to finish bootstrapping so the test can assert that mods
 * were discovered and initialised.
 *
 * Built as part of build.ps1 and run from out/test/, where the loader finds
 * winmm.dll (our shim) before System32.
 */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "winmm.lib")

typedef void (*WaitForCoreFn)(unsigned int);
typedef unsigned int (*CoreReadyFn)(void);
typedef const char* (*VersionFn)(void);
typedef int (*ResolveAnchorFn)(void* module, const char* literal, void** code, size_t* size);

/* Ground truth for the anchor resolver: this function prints a message nobody
 * else in the image carries, so resolving that literal must land back here.
 * Called through a volatile pointer so the optimiser cannot drop it. */
__declspec(noinline) static int host_anchor_probe(int x)
{
    if (x == 12345)
        printf("host: anchor probe message %d\n", x);
    return x + 1;
}

int main(int argc, char** argv)
{
    printf("host: winmm import test\n");

    /* 1. forwarded calls must reach real winmm and return sane values */
    MMRESULT r = timeBeginPeriod(1);
    printf("host: timeBeginPeriod(1)   = %u\n", (unsigned)r);
    DWORD t0 = timeGetTime();
    Sleep(20);
    DWORD t1 = timeGetTime();
    printf("host: timeGetTime delta    = %lu ms\n", (unsigned long)(t1 - t0));
    UINT devs = waveOutGetNumDevs();
    printf("host: waveOutGetNumDevs    = %u\n", (unsigned)devs);
    UINT indevs = waveInGetNumDevs();
    printf("host: waveInGetNumDevs     = %u\n", (unsigned)indevs);
    WAVEOUTCAPSW caps;
    MMRESULT rc = waveOutGetDevCapsW(0, &caps, sizeof(caps));
    printf("host: waveOutGetDevCapsW   = %u\n", (unsigned)rc);
    timeEndPeriod(1);

    int fail = 0;
    if (t1 <= t0)
        { printf("host: FAIL timeGetTime did not advance\n"); fail = 1; }
    if (t1 - t0 < 10)
        { printf("host: FAIL timeGetTime delta too small: %lu\n", (unsigned long)(t1 - t0)); fail = 1; }

    /* 2. the core must be present and must have bootstrapped */
    HMODULE core = GetModuleHandleW(L"aamod_core.dll");
    if (!core) {
        printf("host: FAIL aamod_core.dll was not loaded by the shim\n");
        return 2;
    }
    VersionFn ver = (VersionFn)(void*)GetProcAddress(core, "AAMOD_Version");
    printf("host: aamod core version   = %s\n", ver ? ver() : "(no AAMOD_Version)");
    WaitForCoreFn wait = (WaitForCoreFn)(void*)GetProcAddress(core, "AAMOD_WaitForCore");
    if (wait)
        wait(15000);
    CoreReadyFn ready = (CoreReadyFn)(void*)GetProcAddress(core, "AAMOD_CoreReady");
    if (ready) {
        unsigned int ok = ready();
        printf("host: core bootstrap       = %s\n", ok ? "complete" : "NOT complete");
        if (!ok)
            fail = 1;
    }

    /* 3. report whether a mod directory was seen (info only; asserted by build.ps1) */
    const char* mod_dir = argc > 1 ? argv[1] : "aamod\\mods";
    DWORD attr = GetFileAttributesA(mod_dir);
    printf("host: mod dir (%s)          = %s\n", mod_dir,
           (attr != INVALID_FILE_ATTRIBUTES) ? "present" : "absent");

    /* 4. the runtime anchor resolver must find a function by its message
     *    string, using this process as the search space */
    int (*volatile probe_call)(int) = host_anchor_probe;
    probe_call(0);                                   /* keep the body alive */
    ResolveAnchorFn resolve = (ResolveAnchorFn)(void*)GetProcAddress(core, "AAMOD_ResolveAnchor");
    if (!resolve) {
        printf("host: FAIL aamod_core.dll has no AAMOD_ResolveAnchor\n");
        fail = 1;
    } else {
        void* code = NULL;
        size_t size = 0;
        if (resolve(NULL, "host: anchor probe message %d\n", &code, &size)) {
            uintptr_t begin = (uintptr_t)code;
            uintptr_t probe = (uintptr_t)&host_anchor_probe;
            int inside = probe >= begin && probe < begin + size;
            printf("host: anchor probe         = %p +%zu, probe at %p -> %s\n",
                   code, size, (void*)probe, inside ? "inside" : "OUTSIDE");
            if (!inside)
                fail = 1;
        } else {
            printf("host: FAIL anchor probe did not resolve\n");
            fail = 1;
        }
        /* This literal must be absent, so it has to be built at runtime:
         * anything written in the source would sit in our own .rdata. */
        char missing[64];
        sprintf_s(missing, sizeof(missing), "host: absent literal %c%c%c%c%d",
                  'z', 'q', 'q', 'x', 4711);
        void* bad = (void*)1;
        if (resolve(NULL, missing, &bad, NULL)) {
            printf("host: FAIL negative anchor resolved to %p\n", bad);
            fail = 1;
        } else {
            printf("host: anchor negative      = not found (correct)\n");
        }
    }

    printf("host: %s\n", fail ? "RESULT=FAIL" : "RESULT=OK");
    return fail;
}
