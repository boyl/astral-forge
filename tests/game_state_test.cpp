// Engine-layout fixtures validate the boundary contract, not gameplay behavior.
#include "../src/core/game_state.cpp"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <limits>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #x); return 1; } } while (0)
template<class T> void put(unsigned char* p, size_t offset, T v) { memcpy(p + offset, &v, sizeof(v)); }
static volatile LONG query_failed;
static DWORD WINAPI reader(void*) {
    for (unsigned i = 0; i < 20000; ++i) {
        AAModGameState s = {};
        aamod::game_state_query(&s, sizeof(s));
        if (s.status == AAMOD_STATE_READY && s.players[0].layer_x != double(s.sequence))
            InterlockedExchange(&query_failed, 1);
        AAModStateEvent records[4]; uint32_t count; uint64_t newest;
        if (aamod::game_state_events(0, records, 4, sizeof(records[0]), &count, &newest) != AAMOD_OK)
            InterlockedExchange(&query_failed, 1);
        for (uint32_t j = 0; j < count; ++j) {
            if (!records[j].changes || records[j].id > newest || (j && records[j].id != records[j-1].id + 1))
                InterlockedExchange(&query_failed, 1);
        }
    }
    return 0;
}
int main() {
    using namespace aamod;
    base = (unsigned char*)UINT64_C(0x100000000);
    std::vector<unsigned char> frame(0x1c5010), data(0x50), layers(84 * 0x253c0), object(0xe0);
    auto f = frame.data(), d = data.data(), l = layers.data(), o = object.data();
    put(f, 0x28, int32_t(282)); put(f, 0x30, d); put(f, 0x70, l); put(f, 0x78, uint64_t(84));
    data[8] = 30; memcpy(d + 9, "layout_mainMenu", 15);
    for (unsigned i = 0; i < 84; ++i) put(l + i * 0x253c0, 0x68, l + i * 0x253c0 + 0x10);
    AAModGameState s = {};
    CHECK(sample(f, &s)); CHECK(s.scene_index == 282 && !s.players[0].present && s.scene_epoch == 1);
    // Player in layer 83 catches a previously incorrect 32-layer cap.
    auto last = l + 83 * 0x253c0;
    put(last, 0x68, o); put(o, 0x58, last + 0x10); put(o, 8, base + 0xc407b80);
    put(o, 0x10, double(123.5)); put(o, 0x18, double(-12));
    put(f, 0x1c4ff0, double(173)); put(f, 0x1c4ff8, double(231));
    put(f, 0x1c5000, double(98)); put(f, 0x1c5008, double(419));
    put(f, 0x28, int32_t(268)); put(f, 0xc4, uint32_t(20));
    s = {}; CHECK(sample(f, &s)); CHECK(s.players[0].present && s.players[0].layer_x == 123.5 && s.players[0].layer_y == -12);
    CHECK(s.players[0].valid_fields == (AAMOD_PLAYER_POSITION | AAMOD_PLAYER_HEALTH));
    CHECK(s.players[0].health == 173 && s.players[0].max_health == 231 && s.scene_epoch == 2);
    // Native equipment refresh changes derived stats after an existing player
    // was sampled. Fresh payloads must not count that player twice or lose
    // sequence/thread metadata; cover another effect reversal as well.
    s.size=sizeof(s);s.version=1;s.status=AAMOD_STATE_READY;s.sequence=91;s.update_thread_id=19;s.sampled_at_ms=1234;
    put(f,0x1c4ff0,double(330));put(f,0x1c4ff8,double(330));
    CHECK(resample_after_equipment(f,&s)&&s.players[0].present&&s.players[0].health==330&&s.players[0].max_health==330);
    CHECK(s.sequence==91&&s.update_thread_id==19&&s.sampled_at_ms==1234&&s.scene_epoch==2);
    put(f,0x1c4ff0,double(173));put(f,0x1c4ff8,double(231));
    CHECK(resample_after_equipment(f,&s)&&s.players[0].health==173&&s.players[0].max_health==231&&s.sequence==91);
    put(o, 8, base + 0xc414200); s = {}; CHECK(sample(f, &s));
    CHECK(!s.players[0].present && s.players[1].health == 98 && s.players[1].max_health == 419);
    put(o, 8, base + 0xc407b80);
    put(f, 0x1c4ff0, double(0)); s = {}; CHECK(sample(f, &s));
    CHECK(s.players[0].present && s.players[0].health == 0 && (s.players[0].valid_fields & AAMOD_PLAYER_HEALTH));
    put(f, 0x1c4ff8, double(0)); s = {}; CHECK(sample(f, &s));
    CHECK(s.players[0].valid_fields == AAMOD_PLAYER_POSITION);
    put(f, 0x1c4ff8, double(-1)); s = {}; CHECK(!sample(f, &s));
    put(f, 0x1c4ff8, double(231));
    put(f, 0x1c4ff0, double(-3)); s = {}; CHECK(sample(f, &s)); CHECK(s.players[0].health == -3);
    put(f, 0x1c4ff0, std::numeric_limits<double>::infinity()); s = {}; CHECK(!sample(f, &s));
    put(f, 0x1c4ff0, double(173)); put(f, 0x1c4ff8, std::numeric_limits<double>::quiet_NaN());
    s = {}; CHECK(!sample(f, &s)); put(f, 0x1c4ff8, double(231));
    put(last, 0x68, last + 0x10); s = {}; CHECK(sample(f, &s)); CHECK(!s.players[0].present);
    put(f, 0xc4, uint32_t(0)); s = {}; CHECK(sample(f, &s)); CHECK(s.scene_epoch == 3);
    // Invalid scene, oversized list and a broken ring cannot publish stale data.
    put(f, 0x28, int32_t(293)); s = {}; CHECK(!sample(f, &s)); put(f, 0x28, int32_t(268));
    put(f, 0x78, uint64_t(257)); CHECK(!sample(f, &s)); put(f, 0x78, uint64_t(84));
    put(last, 0x68, o); put(o, 0x58, o); CHECK(!sample(f, &s));
    put(f, 0x30, (unsigned char*)1); CHECK(!sample(f, &s));
    status(AAMOD_STATE_UNSUPPORTED);
    alignas(8) unsigned char buffer[sizeof(AAModGameState) + 8]; memset(buffer, 0xa5, sizeof(buffer));
    CHECK(game_state_query(nullptr, sizeof(s)) == AAMOD_ERR_ARGUMENT);
    CHECK(game_state_query((AAModGameState*)buffer, sizeof(s)-1) == AAMOD_ERR_ARGUMENT);
    CHECK(buffer[0] == 0xa5);
    CHECK(game_state_query((AAModGameState*)buffer, sizeof(buffer)) == 0);
    CHECK(((AAModGameState*)buffer)->status == AAMOD_STATE_UNSUPPORTED && buffer[sizeof(s)] == 0xa5);
    status(AAMOD_STATE_FAULT); CHECK(game_state_query(&s, sizeof(s)) == 0 && !s.players[0].present && s.scene_index == -1);
    HANDLE threads[3];
    for (auto& thread : threads) { thread = CreateThread(nullptr, 0, reader, nullptr, 0, nullptr); CHECK(thread); }
    for (unsigned i = 1; i <= 20000; ++i) {
        s = {}; s.status = AAMOD_STATE_READY; s.sequence = i; s.players[0].layer_x = double(i); publish(s);
    }
    CHECK(WaitForMultipleObjects(3, threads, TRUE, 10000) == WAIT_OBJECT_0);
    for (auto thread : threads) CloseHandle(thread);
    CHECK(!query_failed);
    // Independent cursors, bounded history, no position-only events.
    uint32_t received = 99; uint64_t newest = 0;
    AAModStateEvent batch[4] = {};
    CHECK(game_state_events(0, nullptr, 0, sizeof(batch[0]), &received, &newest) == 0 && !received);
    auto stable = newest;
    s.players[0].layer_x += 1; publish(s);
    CHECK(game_state_events(stable, batch, 4, sizeof(batch[0]), &received, &newest) == 0 && !received && newest == stable);
    s.players[0].present = 1; s.players[0].valid_fields = AAMOD_PLAYER_HEALTH;
    s.players[0].health = 100; s.players[0].max_health = 200; publish(s);
    CHECK(game_state_events(stable, batch, 4, sizeof(batch[0]), &received, &newest) == 0 && received == 1);
    CHECK(batch[0].changes == (AAMOD_EVENT_PLAYERS | AAMOD_EVENT_FIELDS));
    auto first = batch[0].id;
    s.players[0].health = 0; publish(s);
    CHECK(game_state_events(first, batch, 4, sizeof(batch[0]), &received, &newest) == 0 && received == 1);
    CHECK(batch[0].changes == AAMOD_EVENT_HEALTH && batch[0].state.players[0].present && !batch[0].state.players[0].health);
    CHECK(game_state_events(first, batch, 4, sizeof(batch[0]), &received, &newest) == 0 && received == 1);
    s.scene_epoch += 1; publish(s);
    CHECK(game_state_events(newest, batch, 4, sizeof(batch[0]), &received, &newest) == 0 && received == 1 && batch[0].changes == AAMOD_EVENT_SCENE);
    for (unsigned i = 0; i < 130; ++i) { s.players[0].health = double(i + 1); publish(s); }
    CHECK(game_state_events(first, batch, 4, sizeof(batch[0]), &received, &newest) == AAMOD_ERR_EVENT_CURSOR && !received);
    CHECK(game_state_events(newest + 1, batch, 4, sizeof(batch[0]), &received, &newest) == AAMOD_ERR_EVENT_CURSOR && !received);
    CHECK(game_state_events(0, batch, 4, sizeof(batch[0]), &received, &newest) == 0 && received == 4 && batch[0].id == newest - 127);
    CHECK(batch[3].id == batch[0].id + 3);
    received = 77;
    CHECK(game_state_events(0, nullptr, 1, sizeof(batch[0]), &received, &newest) == AAMOD_ERR_ARGUMENT && received == 77);
    CHECK(game_state_events(0, batch, 4, sizeof(batch[0]) - 1, &received, &newest) == AAMOD_ERR_ARGUMENT && received == 77);
    game_state_shutdown(); CHECK(game_state_query(&s, sizeof(s)) == 0 && s.status == AAMOD_STATE_STOPPED);
    puts("game state boundary contracts passed"); return 0;
}
