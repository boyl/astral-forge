#ifndef AAMOD_DATA_H
#define AAMOD_DATA_H
#include <stdint.h>
#define AAMOD_ERR_DATA_IO 22u
#define AAMOD_ERR_DATA_KEY 23u
#define AAMOD_ERR_DATA_OWNER 24u
#define AAMOD_ERR_DATA_MISSING 25u
#define AAMOD_ERR_DATA_BUFFER 26u
#define AAMOD_ERR_DATA_LIMIT 27u
/* Opaque bytes, up to 1 MiB per key. Keys contain 1..64 ASCII letters,
 * digits, '-' or '_'. No paths or game saves. The framework stores data under
 * its writable data directory, namespaced by the manifest plugin id.
 * Limits per plugin: 64 keys and 16 MiB of committed values.
 * read(NULL,0,&size) queries size; short buffers are untouched. Missing keys
 * return DATA_MISSING. Writes flush a temporary file and atomically replace
 * the old value. A failed write keeps the previous value. Empty values are
 * valid; delete is idempotent. Synchronous: use init/a worker, not rendering.
 * Owner tokens are scoped to the current plugin load; ids persist on restart.
 * This isolates accidental cross-plugin access, not malicious native DLLs. */
typedef uint32_t (*AAModDataReadFn)(uint64_t owner,const char* key,void* bytes,uint32_t capacity,uint32_t* size);
typedef uint32_t (*AAModDataWriteFn)(uint64_t owner,const char* key,const void* bytes,uint32_t size);
typedef uint32_t (*AAModDataDeleteFn)(uint64_t owner,const char* key);
#endif
