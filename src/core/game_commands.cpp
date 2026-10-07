#include "game_commands.h"
#include <windows.h>
#include <vector>
#include <algorithm>
#include <cmath>
namespace aamod {
namespace {
SRWLOCK command_lock = SRWLOCK_INIT;
struct Owner { uint64_t token; bool active; };
std::vector<Owner> owners;
uint64_t next_owner, next_id;
bool accepting;
struct Record { uint64_t owner; AAModCommand request; AAModCommandResult result; } records[128];
Owner* find_owner(uint64_t token) { for (auto& owner : owners) if (owner.token == token) return &owner; return nullptr; }
bool valid_owner(uint64_t token) { return token && find_owner(token); }
}
uint64_t game_commands_owner() {
    AcquireSRWLockExclusive(&command_lock);
    uint64_t owner = ++next_owner; owners.push_back({owner, false});
    ReleaseSRWLockExclusive(&command_lock); return owner;
}
void game_commands_activate(uint64_t token) {
    AcquireSRWLockExclusive(&command_lock);
    if (auto owner = find_owner(token)) owner->active = true;
    ReleaseSRWLockExclusive(&command_lock);
}
void game_commands_revoke(uint64_t owner) {
    AcquireSRWLockExclusive(&command_lock);
    owners.erase(std::remove_if(owners.begin(), owners.end(), [owner](const Owner& value) { return value.token == owner; }), owners.end());
    for (auto& record : records) if (record.owner == owner && record.result.id && record.result.status == AAMOD_COMMAND_PENDING)
        record.result.status = AAMOD_COMMAND_CANCELED;
    ReleaseSRWLockExclusive(&command_lock);
}
void game_commands_start() {
    AcquireSRWLockExclusive(&command_lock); accepting = true; ReleaseSRWLockExclusive(&command_lock);
}
bool game_commands_available() {
    AcquireSRWLockShared(&command_lock);bool value=accepting;ReleaseSRWLockShared(&command_lock);return value;
}
uint32_t game_commands_owner_state(uint64_t token) {
    AcquireSRWLockShared(&command_lock);auto owner=find_owner(token);uint32_t state=owner?(owner->active?2u:1u):0u;ReleaseSRWLockShared(&command_lock);return state;
}
void game_commands_stop() {
    AcquireSRWLockExclusive(&command_lock); accepting = false;
    for (auto& record : records) if (record.result.id && record.result.status == AAMOD_COMMAND_PENDING)
        record.result.status = AAMOD_COMMAND_CANCELED;
    ReleaseSRWLockExclusive(&command_lock);
}
uint32_t game_commands_submit(uint64_t owner, const AAModCommand* request, uint32_t size, uint64_t* id) {
    if (!request || !id || size < sizeof(*request) || request->size != sizeof(*request) ||
        (request->kind != AAMOD_COMMAND_HEAL && request->kind != AAMOD_COMMAND_REFILL_MANA) || request->player > 1 || request->reserved ||
        (request->kind == AAMOD_COMMAND_REFILL_MANA && request->player != 0) ||
        !request->scene_epoch || !std::isfinite(request->amount) || request->amount <= 0)
        return AAMOD_ERR_ARGUMENT;
    AcquireSRWLockExclusive(&command_lock);
    uint32_t error = AAMOD_OK;
    if (!valid_owner(owner)) error = AAMOD_ERR_COMMAND_OWNER;
    else if (!accepting) error = AAMOD_ERR_COMMAND_UNAVAILABLE;
    else {
        unsigned pending = 0;
        for (const auto& record : records) pending += record.result.id && record.result.status == AAMOD_COMMAND_PENDING;
        auto& record = records[next_id % 128];
        if (pending >= 64 || (record.result.id && record.result.status == AAMOD_COMMAND_PENDING)) error = AAMOD_ERR_COMMAND_FULL;
        else {
            record = {}; record.owner = owner; record.request = *request;
            record.result.id = ++next_id; record.result.player = request->player;
            record.result.scene_epoch = request->scene_epoch; *id = next_id;
        }
    }
    ReleaseSRWLockExclusive(&command_lock); return error;
}
uint32_t game_commands_result(uint64_t owner, uint64_t id, AAModCommandResult* output, uint32_t size) {
    if (!output || size < sizeof(*output) || !id) return AAMOD_ERR_ARGUMENT;
    AcquireSRWLockShared(&command_lock);
    auto& record = records[(id - 1) % 128]; uint32_t error = AAMOD_OK;
    if (!valid_owner(owner)) error = AAMOD_ERR_COMMAND_OWNER;
    else if (record.result.id != id || record.owner != owner) error = AAMOD_ERR_COMMAND_RESULT;
    else *output = record.result;
    ReleaseSRWLockShared(&command_lock); return error;
}
void game_commands_process(void* frame, AAModGameState* state, HealthWrite write,ManaApply mana) {
    AcquireSRWLockExclusive(&command_lock);
    uint64_t first = next_id > 128 ? next_id - 127 : 1;
    for (uint64_t id = first; id <= next_id; ++id) {
        auto& record = records[(id - 1) % 128];
        if (record.result.status != AAMOD_COMMAND_PENDING) continue;
        auto owner = find_owner(record.owner);
        if (!owner || !owner->active) continue;
        auto& result = record.result; auto& player = state->players[record.request.player];
        result.update_thread_id = state->update_thread_id;
        if (record.request.scene_epoch != state->scene_epoch) result.status = AAMOD_COMMAND_STALE;
        else if (state->status != AAMOD_STATE_READY || (state->scene_index != 266 && state->scene_index != 268) ||
            !player.present || !(player.valid_fields & AAMOD_PLAYER_HEALTH) || player.health <= 0 || player.health > player.max_health)
            result.status = AAMOD_COMMAND_UNAVAILABLE;
        else if(record.request.kind==AAMOD_COMMAND_REFILL_MANA) {
            if(!mana || state->scene_index!=268 || state->players[1].present)result.status=AAMOD_COMMAND_UNAVAILABLE;
            else result.status=mana(frame,0,record.request.amount,&result.before,&result.after);
        } else {
            result.before = player.health;
            double after = (std::min)(player.max_health, player.health + record.request.amount);
            if (write(frame, record.request.player, after)) {
                result.after = after; player.health = after; result.status = AAMOD_COMMAND_APPLIED;
            } else result.status = AAMOD_COMMAND_FAULT;
        }
    }
    ReleaseSRWLockExclusive(&command_lock);
}
}
