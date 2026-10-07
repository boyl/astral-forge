#ifndef AAMOD_NATIVE_IMAGES_H
#define AAMOD_NATIVE_IMAGES_H
#include <stdint.h>
#define AAMOD_ERR_NATIVE_UNSUPPORTED 17u
#define AAMOD_ERR_NATIVE_NOT_READY 18u
#define AAMOD_ERR_NATIVE_CONFLICT 19u
#define AAMOD_ERR_NATIVE_DIMENSIONS 20u
#define AAMOD_ERR_NATIVE_PENDING 21u
#define AAMOD_NATIVE_PENDING 1u
#define AAMOD_NATIVE_APPLIED 2u
#define AAMOD_NATIVE_RESTORE_PENDING 3u
#define AAMOD_NATIVE_RESTORED 4u
#define AAMOD_NATIVE_FAILED 5u
typedef struct AAModNativeImageInfo {
    uint32_t size,image_id,width,height;
    uint32_t buffer_width,buffer_height,format,has_texture;
    uint64_t upload_visits;
} AAModNativeImageInfo;
typedef struct AAModImageReplacementStatus {
    uint32_t size,state,error,image_id;
    uint64_t handle,apply_count,restore_count;
} AAModImageReplacementStatus;
/* Exact-profile only. Metadata is copied, with no raw game pointers. Info is
 * available after the native cache has created the image; upload_visits counts
 * upload-boundary visits, not guaranteed on-screen visibility. */
typedef uint32_t (*AAModNativeImageInfoFn)(uint32_t,AAModNativeImageInfo*,uint32_t);
/* Copies plugin-owned PNG pixels. Dimensions must equal native logical size.
 * One active owner per image ID; up to 16 requests/64 MiB per owner and 128
 * requests/256 MiB total. New requests are pending until the next native draw.
 * Output handle is unchanged on failure. No asset archive or save is written. */
typedef uint32_t (*AAModImageReplaceFn)(uint64_t,uint32_t,const char*,uint64_t*);
/* Asynchronous: the next native draw recreates the original from Assets.dat.
 * Poll status for completion. Plugin cleanup requests the same restoration;
 * core-owned data survives until restoration. No arbitrary texture upload. */
typedef uint32_t (*AAModImageRestoreFn)(uint64_t,uint64_t);
typedef uint32_t (*AAModImageReplacementStatusFn)(uint64_t,uint64_t,AAModImageReplacementStatus*,uint32_t);
/* Release a RESTORED status record so repeated replacements do not fill the
 * bounded handle table. Pending/applied requests return NATIVE_PENDING. */
typedef uint32_t (*AAModImageReplacementForgetFn)(uint64_t,uint64_t);
#endif

