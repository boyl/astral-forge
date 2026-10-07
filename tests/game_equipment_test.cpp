#include "../src/core/game_equipment.cpp"
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <array>
int main() {
    auto base=(unsigned char*)VirtualAlloc(nullptr,0xc0e0000,MEM_RESERVE,PAGE_READWRITE);assert(base);
    assert(VirtualAlloc(base+0xc0df000,0x1000,MEM_COMMIT,PAGE_READWRITE));
    assert(VirtualAlloc(base+0xc0d5000,0x1000,MEM_COMMIT,PAGE_READWRITE));
    std::array<unsigned char,0x250> dictionary={};std::array<unsigned char,16> head={};
    std::array<std::array<unsigned char,0x60>,9> nodes={};
    *(uint32_t*)(base+0xc0dfa20)=2;*(void**)(base+0xc0d5cb0)=dictionary.data();
    *(void**)(dictionary.data()+0x240)=head.data();*(uint64_t*)(dictionary.data()+0x248)=9;
    *(void**)head.data()=nodes[0].data();
    for(unsigned i=0;i<9;++i) {
        auto n=nodes[i].data();*(void**)n=i==8?head.data():nodes[i+1].data();
        char key[16];sprintf_s(key,i<5?"P1_relic_%u":"P1_spell_%u",i<5?i+1:i-4);
        n[16]=20;memcpy(n+17,key,10);*(double*)(n+0x58)=i*3;
    }
    AAModGameState state={};state.status=AAMOD_STATE_READY;state.scene_index=268;state.players[0].present=1;state.sequence=123;state.scene_epoch=4;
    aamod::game_equipment_sample(base,state);AAModEquipment out={};assert(aamod::game_equipment_query(&out,sizeof(out))==0);
    assert(out.valid&&out.sequence==123&&out.scene_epoch==4&&out.auras[4]==12&&out.spells[3]==24);
    // Heap-backed dictionary keys use the game value-string layout, which
    // differs from the 24-byte metadata name-string layout.
    nodes[0][16]=1;*(uint32_t*)(nodes[0].data()+20)=10;*(const char**)(nodes[0].data()+24)="P1_relic_1";
    aamod::game_equipment_sample(base,state);aamod::game_equipment_query(&out,sizeof(out));assert(out.valid);
    for(double bad:{-1.0,0.5,double(INFINITY),double(NAN),double(UINT32_MAX)+1}) {
        *(double*)(nodes[0].data()+0x58)=bad;aamod::game_equipment_sample(base,state);aamod::game_equipment_query(&out,sizeof(out));assert(out.status==AAMOD_STATE_FAULT&&!out.valid&&out.auras[4]==0);
    }
    *(double*)(nodes[0].data()+0x58)=0;
    state.players[1].present=1;aamod::game_equipment_sample(base,state);aamod::game_equipment_query(&out,sizeof(out));assert(!out.valid);
    state.players[1].present=0;state.scene_index=282;aamod::game_equipment_sample(base,state);aamod::game_equipment_query(&out,sizeof(out));assert(!out.valid);
    aamod::game_equipment_status(AAMOD_STATE_STOPPED);aamod::game_equipment_query(&out,sizeof(out));assert(out.status==AAMOD_STATE_STOPPED&&!out.valid);
    assert(aamod::game_equipment_query(&out,sizeof(out)-1)==AAMOD_ERR_ARGUMENT);
    VirtualFree(base,0,MEM_RELEASE);puts("equipment: short/heap strings, copied slots, foreign values and lifecycle passed");
}
