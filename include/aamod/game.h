#ifndef AAMOD_GAME_H
#define AAMOD_GAME_H
#include <stdint.h>
/* Immutable startup identity, not mutable gameplay state or memory integrity. */
#define AAMOD_GAME_INFO_VERSION 1u
#define AAMOD_GAME_UNKNOWN 0u
#define AAMOD_GAME_IDENTITY_MATCH 1u
#define AAMOD_GAME_IDENTITY_UNAVAILABLE 2u
#define AAMOD_ERR_ARGUMENT 3u
typedef struct AAModGameInfo {
    uint32_t size;
    uint32_t version;
    uint32_t identity_status;
    uint32_t steam_app_id; /* zero for unknown hosts */
    uint64_t file_size;
    uint32_t pe_timestamp;
    uint32_t image_size;
    char sha256[65]; /* lower-case hex; empty if hashing failed */
    char profile_id[64]; /* empty for unknown hosts */
    char game_version[32]; /* profile label, never guessed from the filename */
    uint8_t reserved[7];
} AAModGameInfo;
typedef uint32_t (*AAModGameInfoFn)(AAModGameInfo* output, uint32_t output_size);
#endif
