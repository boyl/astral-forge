#include "game_equipment.h"
#include <windows.h>
#include <cstring>
#include <cmath>
namespace aamod {
namespace {
SRWLOCK lock=SRWLOCK_INIT;
AAModEquipment cache={};
// Exact 2.6.4 adapter. Dictionary nodes have a 32-byte native key string
// at +16 and the tagged numerical value at +0x50. Never expose nodes.
bool read_slots(unsigned char* base,AAModEquipment* result,double** slots=nullptr,void** gambits=nullptr) {
    __try {
        if(*(uint32_t*)(base+0xc0dfa20)<2)return true;
        auto dictionary=*(unsigned char**)(base+0xc0d5cb0);
        if(!dictionary)return false;
        auto head=*(unsigned char**)(dictionary+0x240);
        uint64_t count=*(uint64_t*)(dictionary+0x248);
        if(!head || count>20000)return false;
        auto node=*(unsigned char**)head;uint32_t seen=0,gambits_seen=0;
        for(uint64_t i=0;i<count;++i) {
            if(!node || node==head)return false;
            auto key=node+16;uint32_t length=key[0]>>1;const char* text=(const char*)key+1;
            if(key[0]&1){length=*(uint32_t*)(key+4);text=*(const char**)(key+8);}
            if(!text || length>4096)return false;
            int index=-1;
            if(length==10 && !memcmp(text,"P1_relic_",9) && text[9]>='1'&&text[9]<='5')index=text[9]-'1';
            else if(length==10 && !memcmp(text,"P1_spell_",9) && text[9]>='1'&&text[9]<='4')index=5+text[9]-'1';
            if(index>=0) {
                if(seen&(1u<<index) || node[0x50]!=0)return false;
                double id=*(double*)(node+0x58);
                if(!std::isfinite(id)||id<0||id>UINT32_MAX||id!=std::floor(id))return false;
                if(index<5)result->auras[index]=(uint32_t)id;else result->spells[index-5]=(uint32_t)id;
                seen|=1u<<index;
                if(slots)slots[index]=(double*)(node+0x58);
            }
            if(gambits && length==23 && !memcmp(text,"P1_spell_",9) && text[9]>='1'&&text[9]<='4' && !memcmp(text+10,"_modificators",13)) {
                unsigned slot=text[9]-'1';if(gambits_seen&(1u<<slot)||node[0x50]!=1)return false;
                gambits[slot]=node+0x58;gambits_seen|=1u<<slot;
            }
            node=*(unsigned char**)node;
        }
        if(seen!=511 || (gambits && gambits_seen!=15))return false;
        result->valid=1;return true;
    } __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return false;}
}
void publish(const AAModEquipment& value){AcquireSRWLockExclusive(&lock);cache=value;ReleaseSRWLockExclusive(&lock);}
}
bool game_equipment_fields(unsigned char* base,double** slots,void** gambits) {
    AAModEquipment copied={};return read_slots(base,&copied,slots,gambits)&&copied.valid;
}
void game_equipment_status(uint32_t status){AAModEquipment value={};value.size=sizeof(value);value.status=status;publish(value);}
void game_equipment_sample(unsigned char* base,const AAModGameState& state) {
    AAModEquipment value={};value.size=sizeof(value);value.status=state.status;value.sequence=state.sequence;value.scene_epoch=state.scene_epoch;
    if(state.status==AAMOD_STATE_READY && state.scene_index==268 && state.players[0].present && !state.players[1].present) {
        if(!read_slots(base,&value)){value={};value.size=sizeof(value);value.status=AAMOD_STATE_FAULT;value.sequence=state.sequence;value.scene_epoch=state.scene_epoch;}
    }
    publish(value);
}
uint32_t game_equipment_query(AAModEquipment* out,uint32_t size) {
    if(!out || size<sizeof(*out))return AAMOD_ERR_ARGUMENT;
    AcquireSRWLockShared(&lock);*out=cache;ReleaseSRWLockShared(&lock);return AAMOD_OK;
}
}
