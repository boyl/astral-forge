#ifndef AAMOD_RESOURCES_H
#define AAMOD_RESOURCES_H
#include <stdint.h>
#define AAMOD_ERR_RESOURCE_IO 10u
#define AAMOD_ERR_RESOURCE_FORMAT 11u
#define AAMOD_ERR_RESOURCE_LIMIT 12u
#define AAMOD_ERR_RESOURCE_HANDLE 13u
#define AAMOD_ERR_RESOURCE_PATH 14u
#define AAMOD_ERR_RESOURCE_COM 15u
#define AAMOD_ERR_RESOURCE_MEMORY 16u
typedef struct AAModImage {
    uint32_t size;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t byte_count;
    uint32_t reserved;
    uint64_t handle;
    const uint8_t* pixels; /* immutable straight-alpha RGBA8, not a game pointer */
} AAModImage;
/* Synchronous PNG decoding from a UTF-8 path relative to this plugin's root.
 * Rejects absolute paths, traversal, ADS and resolved links outside that root.
 * Call during init/on a worker, not from a render callback. No native game
 * asset is replaced. Output is unchanged on failure. One load owns one handle;
 * pixels remain valid until release/owner cleanup, which must be coordinated
 * with the plugin's readers. Core closes remaining images after mod shutdown.
 * Limits: 32 MiB encoded, 8192 per axis, 64 MiB decoded, 16 images/64 MiB per
 * plugin, 256 MiB total. Owners cannot release another plugin's image. */
typedef uint32_t (*AAModImageLoadFn)(uint64_t owner,const char* relative_png,AAModImage*,uint32_t);
typedef uint32_t (*AAModImageReleaseFn)(uint64_t owner,uint64_t image);
#endif
