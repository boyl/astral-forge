/* aamod sample mod: "hello".
 *
 * Exercises the whole M0 surface: logging, config, allocation, and the inline
 * hook primitive. The hook self-test patches three functions of this module
 * with deliberately different prologues (plain arithmetic, RIP-relative global
 * access, floating point) and checks that the detour runs, that the trampoline
 * reaches the original, and that removal restores the original bytes.
 */
#include "aamod/aamod.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

static const AAModAPI* g_api = NULL;

/* ---- hook self-test ------------------------------------------------------ */
static volatile int g_sum = 0;
static volatile int g_hits_multiply = 0;
static volatile int g_hits_globalsum = 0;
static volatile int g_hits_vectors = 0;

static int   (*g_tramp_multiply)(int, int) = NULL;
static int   (*g_tramp_globalsum)(int) = NULL;
static float (*g_tramp_vectors)(const float*, float*) = NULL;

static int   hooked_multiply(int a, int b)
{
    ++g_hits_multiply;
    return g_tramp_multiply ? g_tramp_multiply(a, b) : 0;
}

static int   hooked_globalsum(int a)
{
    ++g_hits_globalsum;
    return g_tramp_globalsum ? g_tramp_globalsum(a) : 0;
}

static float hooked_vectors(const float* in, float* out)
{
    ++g_hits_vectors;
    return g_tramp_vectors ? g_tramp_vectors(in, out) : 0.0f;
}

__declspec(noinline) static int mod_multiply(int a, int b)
{
    volatile int pad[4];
    pad[0] = a;
    pad[1] = b;
    pad[2] = a + b;
    pad[3] = a - b;
    return pad[2] + pad[3] + (pad[0] * pad[1]) - (a + b) + (a + b);
}

__declspec(noinline) static int mod_globalsum(int a)
{
    volatile int local[6];
    for (int i = 0; i < 6; ++i)
        local[i] = a + i;
    int s = 0;
    for (int i = 0; i < 6; ++i)
        s += local[i];
    g_sum += s;                       // RIP-relative access to a module global
    return s;                         // pure result: the test compares calls
}

__declspec(noinline) static float mod_vectors(const float* in, float* out)
{
    volatile float tmp[4];
    tmp[0] = in[0] * 0.5f;
    tmp[1] = in[1] * 2.0f;
    tmp[2] = tmp[0] + tmp[1];
    tmp[3] = tmp[0] - tmp[1];
    out[0] = tmp[2];
    out[1] = tmp[3];
    return tmp[2] + tmp[3];
}

static void report(const char* name, bool ok, const char* detail)
{
    if (ok)
        AAMOD_LOGI(g_api, "hello: hook self-test %s: OK (%s)", name, detail);
    else
        AAMOD_LOGE(g_api, "hello: hook self-test %s: FAILED (%s)", name, detail);
}

static void run_hook_selftest(void)
{
    int ok = 0;

    {   /* 1: integer arithmetic, register spills */
        int (*fn)(int, int) = &mod_multiply;
        int before = fn(6, 7);
        void* tramp = NULL;
        if (g_api->hook_install((void*)mod_multiply, (void*)hooked_multiply, &tramp) == AAMOD_OK) {
            g_tramp_multiply = (int (*)(int, int))tramp;
            int after = fn(6, 7);
            int hits = g_hits_multiply;
            uint32_t rrc = g_api->hook_remove((void*)mod_multiply);
            g_tramp_multiply = NULL;
            int restored = fn(6, 7);
            bool good = (after == before && hits == 1 && restored == before && rrc == AAMOD_OK);
            char detail[128];
            _snprintf_s(detail, _TRUNCATE, "value=%d hits=%d restored=%d", after, hits, restored);
            report("multiply", good, detail);
            ok += good;
        } else {
            report("multiply", false, "install failed");
        }
    }

    {   /* 2: touches a module global (RIP-relative operand in the prologue) */
        int (*fn)(int) = &mod_globalsum;
        int before = fn(3);
        void* tramp = NULL;
        if (g_api->hook_install((void*)mod_globalsum, (void*)hooked_globalsum, &tramp) == AAMOD_OK) {
            g_tramp_globalsum = (int (*)(int))tramp;
            int after = fn(3);
            int hits = g_hits_globalsum;
            uint32_t rrc = g_api->hook_remove((void*)mod_globalsum);
            g_tramp_globalsum = NULL;
            int restored = fn(3);
            bool good = (after == before && hits == 1 && restored == before && rrc == AAMOD_OK);
            char detail[128];
            _snprintf_s(detail, _TRUNCATE, "value=%d hits=%d restored=%d", after, hits, restored);
            report("globalsum", good, detail);
            ok += good;
        } else {
            report("globalsum", false, "install failed");
        }
    }

    {   /* 3: floating point path (xmm saves in the prologue) */
        float in[2] = { 2.0f, 4.0f };
        float out[2] = { 0.0f, 0.0f };
        float (*fn)(const float*, float*) = &mod_vectors;
        float before = fn(in, out);
        void* tramp = NULL;
        if (g_api->hook_install((void*)mod_vectors, (void*)hooked_vectors, &tramp) == AAMOD_OK) {
            g_tramp_vectors = (float (*)(const float*, float*))tramp;
            float after = fn(in, out);
            int hits = g_hits_vectors;
            uint32_t rrc = g_api->hook_remove((void*)mod_vectors);
            g_tramp_vectors = NULL;
            float restored = fn(in, out);
            bool good = (after == before && hits == 1 && restored == before && rrc == AAMOD_OK);
            char detail[128];
            _snprintf_s(detail, _TRUNCATE, "value=%.2f hits=%d restored=%.2f",
                        (double)after, hits, (double)restored);
            report("vectors", good, detail);
            ok += good;
        } else {
            report("vectors", false, "install failed");
        }
    }

    if (ok == 3)
        AAMOD_LOGI(g_api, "hello: hook self-test 3/3 OK");
    else
        AAMOD_LOGE(g_api, "hello: hook self-test %d/3 OK", ok);
}

AAMOD_EXPORT uint32_t AAMOD_Init(const AAModAPI* api, uint32_t api_size)
{
    if (!api || api->api_version != AAMOD_ABI_VERSION)
        return AAMOD_ERR_ABI;

    g_api = api;
    if (api_size < sizeof(AAModAPI)) {
        AAMOD_LOGW(api, "hello: loader api_size=%u is smaller than this mod expects (%u)",
                   api_size, (unsigned)sizeof(AAModAPI));
    }

    AAMOD_LOGI(api, "hello: AAMOD_Init (api %u/%u bytes)", api->api_version, api_size);
    AAMOD_LOGI(api, "hello: game_dir=%s", api->game_dir ? api->game_dir : "(null)");
    AAMOD_LOGI(api, "hello: mod_dir =%s", api->mod_dir ? api->mod_dir : "(null)");
    AAMOD_LOGI(api, "hello: greeting=%s", api->config_str("hello.greeting", "(none)"));

    void* scratch = api->alloc(64);
    AAMOD_LOGI(api, "hello: alloc(64) = %s", scratch ? "ok" : "failed");
    if (scratch)
        api->free_(scratch);

    run_hook_selftest();

    AAMOD_LOGI(api, "hello: init complete");
    return AAMOD_OK;
}

AAMOD_EXPORT void AAMOD_Shutdown(void)
{
    if (g_api)
        AAMOD_LOGI(g_api, "hello: AAMOD_Shutdown");
}

AAMOD_EXPORT const char* AAMOD_ModInfo(void)
{
    return "hello 0.1.0 - aamod M0 sample mod";
}
