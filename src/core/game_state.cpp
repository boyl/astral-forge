#include <windows.h>
#include <cstring>
#include <cmath>
#include "game_state.h"
#include "game_profile.h"
#include "game_commands.h"
#include "game_equipment.h"
#include "game_combat.h"
#include "equipment_commands.h"
#include "native_equipment.h"
#include "content_catalog.h"
#include "log.h"

namespace aamod {
namespace {
SRWLOCK lock = SRWLOCK_INIT;
AAModGameState cache = {};
AAModStateEvent events[128] = {};
uint64_t newest_event;
unsigned char* base;
void* table[5];
void* original_table;
void (*original_post)(void*);
HANDLE worker;
volatile LONG stopping;
uint64_t sequence, epoch;
int previous_scene = -1;
uint32_t previous_loop;
DWORD update_thread;

void publish(const AAModGameState& value) {
    AcquireSRWLockExclusive(&lock);
    uint32_t changes = 0;
    if (!newest_event || cache.status != value.status) changes |= AAMOD_EVENT_STATUS;
    if (cache.scene_index != value.scene_index || cache.scene_epoch != value.scene_epoch)
        changes |= AAMOD_EVENT_SCENE;
    for (unsigned i = 0; i < 2; ++i) {
        const auto& before = cache.players[i]; const auto& after = value.players[i];
        if (before.present != after.present) changes |= AAMOD_EVENT_PLAYERS;
        if (before.valid_fields != after.valid_fields) changes |= AAMOD_EVENT_FIELDS;
        if ((before.valid_fields & AAMOD_PLAYER_HEALTH) && (after.valid_fields & AAMOD_PLAYER_HEALTH)
            && (before.health != after.health || before.max_health != after.max_health))
            changes |= AAMOD_EVENT_HEALTH;
    }
    if (changes) {
        uint64_t id = ++newest_event;
        auto& event = events[(id - 1) % 128];
        event = {}; event.id = id; event.changes = changes; event.state = value;
    }
    cache = value;
    ReleaseSRWLockExclusive(&lock);
}
void status(uint32_t value) {
    game_equipment_status(value);
    game_combat_status(value);
    AAModGameState next = {};
    next.size = sizeof(next); next.version = AAMOD_STATE_VERSION;
    next.status = value; next.scene_index = -1;
    publish(next);
}
// All foreign object reads are restricted to the exact executable profile and
// the engine post-event update boundary. No pointers survive this function.
bool sample(unsigned char* frame, AAModGameState* out) {
    __try {
        out->scene_index = *(int32_t*)(frame + 0x28);
        auto data = *(unsigned char**)(frame + 0x30);
        if (!data || out->scene_index < 0 || out->scene_index >= 293) return false;
        const unsigned char* name = data + 8;
        uint64_t length = name[0] >> 1;
        const char* text = (const char*)name + 1;
        if (name[0] & 1) { length = *(uint64_t*)(name + 8); text = *(const char**)(name + 16); }
        if (length >= sizeof(out->scene_name) || !text) return false;
        memcpy(out->scene_name, text, (size_t)length);
        auto layers = *(unsigned char**)(frame + 0x70);
        uint64_t count = *(uint64_t*)(frame + 0x78);
        if (count > 256 || (count && !layers)) return false;
        for (uint64_t i = 0; i < count; ++i) {
            auto layer = layers + i * 0x253c0;
            auto sentinel = layer + 0x10;
            auto object = *(unsigned char**)(layer + 0x68);
            unsigned visited = 0;
            while (object != sentinel) {
                if (!object || ++visited > 20000) return false;
                auto metadata = *(void**)(object + 8);
                int player = metadata == base + 0xc407b80 ? 0 : metadata == base + 0xc414200 ? 1 : -1;
                if (player >= 0) {
                    if (out->players[player].present) return false;
                    auto& p = out->players[player];
                    p.layer_x = *(double*)(object + 0x10); p.layer_y = *(double*)(object + 0x18);
                    if (!std::isfinite(p.layer_x) || !std::isfinite(p.layer_y)) return false;
                    p.present = 1; p.valid_fields = AAMOD_PLAYER_POSITION;
                    // Native gameProgress bindings, independently checked with
                    // distinct isolated-save values for both players.
                    double health = *(double*)(frame + 0x1c4ff0 + player * 16);
                    double maximum = *(double*)(frame + 0x1c4ff8 + player * 16);
                    if (!std::isfinite(health) || !std::isfinite(maximum) || maximum < 0) return false;
                    if (maximum > 0) {
                        p.health = health; p.max_health = maximum;
                        p.valid_fields |= AAMOD_PLAYER_HEALTH;
                    }
                }
                object = *(unsigned char**)(object + 0x58);
            }
        }
        uint32_t loop = *(uint32_t*)(frame + 0xc4);
        if (out->scene_index != previous_scene || loop < previous_loop) ++epoch;
        previous_scene = out->scene_index; previous_loop = loop;
        out->scene_epoch = epoch;
        return true;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}
bool resample_after_equipment(void* frame,AAModGameState* current) {
    AAModGameState next={};next.size=current->size;next.version=current->version;
    next.status=current->status;next.sequence=current->sequence;
    next.update_thread_id=current->update_thread_id;next.sampled_at_ms=current->sampled_at_ms;
    if(!sample((unsigned char*)frame,&next))return false;
    *current=next;return true;
}
bool write_health(void* frame, unsigned player, double value) {
    __try {
        auto health = (double*)((unsigned char*)frame + 0x1c4ff0 + player * 16);
        *health = value; return *health == value;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}
void post(void* frame) {
    original_post(frame);
    if (InterlockedCompareExchange(&stopping, 0, 0)) return;
    AAModGameState next = {};
    next.size = sizeof(next); next.version = AAMOD_STATE_VERSION;
    next.sequence = ++sequence; next.update_thread_id = GetCurrentThreadId();
    next.sampled_at_ms = GetTickCount64();
    if (!update_thread) update_thread = next.update_thread_id;
    if (next.update_thread_id != update_thread || !sample((unsigned char*)frame, &next)) {
        // Never retain the last scene or player after an invalid foreign read.
        status(AAMOD_STATE_FAULT); InterlockedExchange(&stopping, 1);
        game_commands_stop();
        equipment_stop();
        AAMOD_ERROR("game state: foreign layout invalid; sampling stopped"); return;
    }
    next.status = AAMOD_STATE_READY;
    game_commands_process(frame, &next, write_health,game_combat_refill);
    NativeEquipmentContext equipment_context={base,frame};
    bool equipment_called=equipment_process(&equipment_context,next,native_equipment_apply,native_equipment_checkpoint);
    // Native refresh can change derived stats; copy the resulting frame.
    if(equipment_called && !resample_after_equipment(frame,&next)) {
        status(AAMOD_STATE_FAULT);InterlockedExchange(&stopping,1);game_commands_stop();equipment_stop();
        AAMOD_ERROR("game state: post-command foreign layout invalid; sampling stopped");return;
    }
    game_combat_sample(frame,next);
    game_equipment_sample(base,next);
    publish(next);
}
DWORD WINAPI attach(void*) {
    auto slot = (void**)(base + 0xa12acc8);
    for (unsigned i = 0; i < 1000 && !InterlockedCompareExchange(&stopping, 0, 0); ++i) {
        void* current = InterlockedCompareExchangePointer(slot, nullptr, nullptr);
        if (current == base + 0x5a1bb60) {
            memcpy(table, current, sizeof(table)); original_table = current;
            original_post = (void(*)(void*))table[1]; table[1] = (void*)post;
            if (InterlockedCompareExchangePointer(slot, table, current) == current) {
                AAMOD_INFO("game state: attached native post-update boundary"); return 0;
            }
        }
        Sleep(10);
    }
    if (!InterlockedCompareExchange(&stopping, 0, 0)) {
        game_commands_stop();
        equipment_stop();
        status(AAMOD_STATE_FAULT); AAMOD_ERROR("game state: native frame constructor not observed or vtable already replaced");
    }
    return 1;
}
}
void game_state_initialize() {
    AAModGameInfo identity = {}; game_profile_query(&identity, sizeof(identity));
    status(AAMOD_STATE_UNSUPPORTED);
    if (identity.identity_status != AAMOD_GAME_IDENTITY_MATCH) return;
    // Preview commands remain opt-in until their real gameplay validation.
    char experimental[8] = {};
    if (GetEnvironmentVariableA("AAMOD_EXPERIMENTAL_COMMANDS", experimental, sizeof(experimental)) == 1 && experimental[0] == '1') {
        game_commands_start();
        if(content_catalog_available())equipment_start();
    }
    base = (unsigned char*)GetModuleHandleW(nullptr);
    InterlockedExchange(&stopping, 0); sequence = epoch = 0; previous_scene = -1; previous_loop = 0; update_thread = 0;
    status(AAMOD_STATE_WAITING);
    worker = CreateThread(nullptr, 0, attach, nullptr, 0, nullptr);
    if (!worker) { game_commands_stop(); equipment_stop(); status(AAMOD_STATE_FAULT); AAMOD_ERROR("game state: attachment worker creation failed"); }
}
void game_state_shutdown() {
    game_commands_stop();
    equipment_stop();
    InterlockedExchange(&stopping, 1);
    if (worker) { WaitForSingleObject(worker, INFINITE); CloseHandle(worker); worker = nullptr; }
    if (base && original_table) {
        if (InterlockedCompareExchangePointer((void**)(base + 0xa12acc8), original_table, table) != table)
            AAMOD_WARN("game state: frame vtable changed externally; not overwritten");
    }
    status(AAMOD_STATE_STOPPED);
}
uint32_t game_state_query(AAModGameState* output, uint32_t size) {
    if (!output || size < sizeof(*output)) return AAMOD_ERR_ARGUMENT;
    AcquireSRWLockShared(&lock); memcpy(output, &cache, sizeof(cache)); ReleaseSRWLockShared(&lock);
    return 0;
}
uint32_t game_state_events(uint64_t after, AAModStateEvent* output, uint32_t capacity,
    uint32_t event_size, uint32_t* count, uint64_t* newest) {
    if (!count || !newest || event_size != sizeof(AAModStateEvent) || (capacity && !output))
        return AAMOD_ERR_ARGUMENT;
    AcquireSRWLockShared(&lock);
    *count = 0; *newest = newest_event;
    uint64_t oldest = newest_event > 128 ? newest_event - 127 : 1;
    if (after > newest_event || (after && after < oldest - 1)) {
        ReleaseSRWLockShared(&lock); return AAMOD_ERR_EVENT_CURSOR;
    }
    uint64_t first = after ? after + 1 : oldest;
    for (uint64_t id = first; id <= newest_event && *count < capacity; ++id)
        output[(*count)++] = events[(id - 1) % 128];
    ReleaseSRWLockShared(&lock); return AAMOD_OK;
}
}
