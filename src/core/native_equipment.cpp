#include "native_equipment.h"
#include "game_equipment.h"
#include "log.h"
#include "../vendor/nlohmann/json.hpp"
#include <windows.h>
#include <string>
#include <cstring>
#include <cmath>
namespace aamod {
namespace {
using Json=nlohmann::json;
bool string_read(void* value,char* output,uint32_t capacity,uint32_t* length) {
    __try {
        auto bytes=(unsigned char*)value;uint32_t size=bytes[0]>>1;const char* text=(const char*)bytes+1;
        if(bytes[0]&1){size=*(uint32_t*)(bytes+4);text=*(const char**)(bytes+8);}
        if(size>4*1024*1024 || (size&&!text))return false;
        *length=size;if(!output)return true;if(size>capacity)return false;
        if(size)memcpy(output,text,size);return true;
    } __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return false;}
}
bool string_copy(void* value,std::string& text) {
    uint32_t length;if(!string_read(value,nullptr,0,&length))return false;text.resize(length);
    uint32_t got;return string_read(value,text.data(),length,&got)&&got==length;
}
bool room(NativeEquipmentContext& context,uint32_t* result) {
    __try {
        unsigned char value[72]={};
        auto get=(void*(*)(void*,void*,void*))(context.base+0x499c1e0);
        auto progress=*(void**)(context.base+0xc08c600);if(!progress)return false;
        get(progress,value,context.base+0xc43d250);
        double number=*(double*)(value+8);
        if(value[0]!=0||!std::isfinite(number)||number<0||number>UINT32_MAX||number!=std::floor(number))return false;
        *result=(uint32_t)number;return true;
    } __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return false;}
}
bool cached_checkpoint(NativeEquipmentContext& context,void** output) {
    __try {
        if(*(uint32_t*)(context.base+0xc0b7ca0)<2)return false;
        auto object=*(unsigned char**)(context.base+0xc0b7cb0);if(!object)return false;
        auto variables=*(unsigned char**)(object+0x38);if(!variables)return false;
        *output=variables+0x40;return true;
    } __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return false;}
}
struct Checkpoint {void* value;Json document;};
bool prepare(NativeEquipmentContext& context,Checkpoint& checkpoint) {
    if(!cached_checkpoint(context,&checkpoint.value))return false;
    std::string text;if(!string_copy(checkpoint.value,text))return false;
    checkpoint.document=Json::parse(text);
    const auto& equipment=checkpoint.document.at("playerSpells").at("data");
    for(unsigned slot=1;slot<=5;++slot)if(!equipment.at("P1_relic_"+std::to_string(slot)).is_number())return false;
    for(unsigned slot=1;slot<=4;++slot)if(!equipment.at("P1_spell_"+std::to_string(slot)).is_number()||!equipment.at("P1_spell_"+std::to_string(slot)+"_modificators").is_string())return false;
    return true;
}
bool assign_and_save(NativeEquipmentContext& context,void* target,const char* text,uint32_t length) {
    __try {
        auto assign=(void(*)(void*,const char*,uint32_t))(context.base+0x63f9a0);
        auto save=(void(*)(void*))(context.base+0x15fda50);
        assign(target,text,length);*(double*)((unsigned char*)context.frame+0x1c95e0)=1;save(context.frame);return true;
    } __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return false;}
}
bool save(NativeEquipmentContext& context,Checkpoint& checkpoint,double** slots,void** gambits) {
    auto& equipment=checkpoint.document.at("playerSpells").at("data");
    for(unsigned slot=0;slot<5;++slot)equipment["P1_relic_"+std::to_string(slot+1)]=*slots[slot];
    for(unsigned slot=0;slot<4;++slot) {
        std::string text;if(!string_copy(gambits[slot],text))return false;
        equipment["P1_spell_"+std::to_string(slot+1)]=*slots[slot+5];equipment["P1_spell_"+std::to_string(slot+1)+"_modificators"]=text;
    }
    auto text=checkpoint.document.dump(-1,' ',true);
    return assign_and_save(context,checkpoint.value,text.data(),(uint32_t)text.size());
}
// Native loot-swap finalization writes the spell icon's current definition
// (RVA 0x31fb416). Its previous-definition field is left for the engine's
// normal change detection; refreshing gambits does not perform this write.
bool spell_icon(NativeEquipmentContext& context,uint32_t slot,uint32_t expected,double** current) {
    __try {
        auto frame=(unsigned char*)context.frame;
        auto layers=*(unsigned char**)(frame+0x70);auto count=*(uint64_t*)(frame+0x78);
        if(count>256 || (count&&!layers))return false;
        *current=nullptr;
        for(uint64_t i=0;i<count;++i) {
            auto layer=layers+i*0x253c0;auto sentinel=layer+0x10;
            auto object=*(unsigned char**)(layer+0x68);unsigned visited=0;
            while(object!=sentinel) {
                if(!object || ++visited>20000)return false;
                if(*(uint32_t*)(object+0x68)==0x1aa0) {
                    auto variables=*(unsigned char**)(object+0x38);
                    if(*(double*)(variables+0x880)==double(slot)+1 && *(double*)(variables+0x888)==1.0) {
                        auto definition=(double*)(variables+0x8a8);
                        if(*current || *definition!=expected)return false;
                        *current=definition;
                    }
                }
                object=*(unsigned char**)(object+0x58);
            }
        }
        return *current!=nullptr;
    } __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return false;}
}
bool mutate(NativeEquipmentContext& context,const AAModEquipmentCommand& request,double** slots,void** gambits,const std::string& replacement) {
    __try {
        double* icon=nullptr;
        if(request.kind==AAMOD_CONTENT_SPELL && !spell_icon(context,request.slot,request.expected_id,&icon))return false;
        if(request.kind==AAMOD_CONTENT_SPELL) {
            auto assign=(void(*)(void*,const char*,uint32_t))(context.base+0x63f9a0);
            assign(gambits[request.slot],replacement.data(),(uint32_t)replacement.size());
        }
        unsigned index=request.kind==AAMOD_CONTENT_AURA?request.slot:request.slot+5;*slots[index]=request.id;
        if(request.kind==AAMOD_CONTENT_SPELL) {
            *icon=request.id;
        }
        ((void(*)(void*))(context.base+0x1450440))(context.frame);
        if(request.kind==AAMOD_CONTENT_SPELL) {
            native_refresh_spell((NativeSpellRefreshFn)(context.base+0x3582dc0),context.frame,request.slot);
        }
        return *slots[index]==request.id;
    } __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return false;}
}
}
void native_equipment_apply(void* raw,const AAModEquipmentCommand& request,AAModEquipmentResult& result) {
    auto& context=*(NativeEquipmentContext*)raw;double* slots[9]={};void* gambits[4]={};
    if(!game_equipment_fields(context.base,slots,gambits)){result.status=AAMOD_EQUIPMENT_FAULT;return;}
    unsigned index=request.kind==AAMOD_CONTENT_AURA?request.slot:request.slot+5;
    result.before=(uint32_t)*slots[index];result.after=result.before;
    if(result.before!=request.expected_id){result.status=AAMOD_EQUIPMENT_CONFLICT;result.error=AAMOD_ERR_EQUIPMENT_CONFLICT;return;}
    bool checkpoint_requested=(request.flags&AAMOD_EQUIPMENT_SAVE_CHECKPOINT)!=0;uint32_t current_room=0;Checkpoint checkpoint={};
    try {
        if(checkpoint_requested) {
            if(!room(context,&current_room)){result.status=AAMOD_EQUIPMENT_FAULT;return;}
            if(current_room && !prepare(context,checkpoint)){result.status=AAMOD_EQUIPMENT_UNAVAILABLE;return;}
        }
        std::string replacement;
        if(request.kind==AAMOD_CONTENT_SPELL) {
            std::string original;
            if(!string_copy(gambits[request.slot],original)||!native_clear_gambits(original,replacement)){result.status=AAMOD_EQUIPMENT_FAULT;return;}
        }
        if(!mutate(context,request,slots,gambits,replacement)){result.status=AAMOD_EQUIPMENT_FAULT;return;}
        result.after=request.id;result.status=AAMOD_EQUIPMENT_APPLIED;
        if(checkpoint_requested)result.checkpoint_status=current_room?(save(context,checkpoint,slots,gambits)?AAMOD_CHECKPOINT_SAVE_DISPATCHED:AAMOD_CHECKPOINT_FAILED):AAMOD_CHECKPOINT_PENDING;
    } catch(const Json::exception& e){result.error=AAMOD_ERR_CONTENT_FORMAT;if(result.status==AAMOD_EQUIPMENT_APPLIED)result.checkpoint_status=AAMOD_CHECKPOINT_FAILED;else result.status=AAMOD_EQUIPMENT_FAULT;AAMOD_ERROR("equipment checkpoint: %s",e.what());}
}
void native_equipment_checkpoint(void* raw,const AAModEquipmentCommand& request,AAModEquipmentResult& result) {
    auto& context=*(NativeEquipmentContext*)raw;uint32_t current_room;
    if(!room(context,&current_room)){result.checkpoint_status=AAMOD_CHECKPOINT_FAILED;return;}
    if(!current_room)return;
    double* slots[9]={};void* gambits[4]={};
    if(!game_equipment_fields(context.base,slots,gambits)){result.checkpoint_status=AAMOD_CHECKPOINT_FAILED;return;}
    unsigned index=request.kind==AAMOD_CONTENT_AURA?request.slot:request.slot+5;
    if(*slots[index]!=request.id){result.checkpoint_status=AAMOD_CHECKPOINT_FAILED;result.error=AAMOD_ERR_EQUIPMENT_CONFLICT;return;}
    try {Checkpoint checkpoint={};if(!prepare(context,checkpoint))return;result.checkpoint_status=save(context,checkpoint,slots,gambits)?AAMOD_CHECKPOINT_SAVE_DISPATCHED:AAMOD_CHECKPOINT_FAILED;}
    catch(const Json::exception& e){result.checkpoint_status=AAMOD_CHECKPOINT_FAILED;result.error=AAMOD_ERR_CONTENT_FORMAT;AAMOD_ERROR("equipment checkpoint: %s",e.what());}
}
}

