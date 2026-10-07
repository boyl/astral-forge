#pragma once
#include "aamod/aamod.h"
#include <string>
#include <string_view>
namespace aamod {
inline bool native_clear_gambits(std::string_view original,std::string& replacement) {
    size_t start=0;
    for(unsigned i=0;i<4;++i) {auto separator=original.find('*',start);if(separator==std::string_view::npos)return false;start=separator+1;}
    // Native runs append upgrades and identity after the four slot states.
    // Preserve that suffix verbatim; only the four equipped gambits change.
    auto suffix=original.substr(start);auto separator=suffix.find('*');
    auto states=suffix.substr(0,separator);if(states.size()!=4)return false;
    for(char digit:states)if(digit<'0'||digit>'3')return false;
    replacement="0*0x*0x*0x*";replacement.append(suffix.data(),suffix.size());return true;
}
using NativeSpellRefreshFn=double(*)(void*,double,double);
inline double native_refresh_spell(NativeSpellRefreshFn refresh,void* frame,uint32_t slot) {return refresh(frame,1.0,double(slot)+1.0);}
struct NativeEquipmentContext {unsigned char* base;void* frame;};
void native_equipment_apply(void*,const AAModEquipmentCommand&,AAModEquipmentResult&);
void native_equipment_checkpoint(void*,const AAModEquipmentCommand&,AAModEquipmentResult&);
}
