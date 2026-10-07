#ifndef AAMOD_CATALOG_H
#define AAMOD_CATALOG_H
#include <stdint.h>
#define AAMOD_CONTENT_AURA 1u
#define AAMOD_CONTENT_SPELL 2u
#define AAMOD_ERR_CONTENT_UNSUPPORTED 28u
#define AAMOD_ERR_CONTENT_FORMAT 29u
#define AAMOD_ERR_CONTENT_BUFFER 30u
#define AAMOD_ERR_CONTENT_UNAVAILABLE 31u
typedef struct AAModCatalogEntry {
    uint32_t size;
    uint32_t kind;
    uint32_t id;
    uint32_t quality; /* Aura: 1..4. Spell: 0, not a rarity claim. */
    char name[192]; /* UTF-8, markup removed, NUL terminated */
} AAModCatalogEntry;
/* Caller-owned paged copies, sorted by game component id. Languages: zh-CN,
 * en. capacity=0 queries total without entries. Spell catalog intentionally
 * exposes the native common-spell book (30..86), not every scripted ability.
 * This lists existing game content; it does not register new components. */
typedef uint32_t (*AAModCatalogFn)(uint32_t kind,const char* language,uint32_t offset,AAModCatalogEntry*,uint32_t capacity,uint32_t entry_size,uint32_t* count,uint32_t* total);
#endif
