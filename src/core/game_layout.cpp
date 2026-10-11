#include "game_layout.h"
#include "log.h"
#include <cstring>
#include <vector>
#include <algorithm>
namespace aamod {
GameLayout game_layout;
namespace {
enum class Kind { entry, call, rip, field, leaf, site };
struct Pattern {const unsigned char* bytes;const char* mask;size_t size;Kind kind;size_t instruction,size_instruction,operand,size_operand;};
#include "game_layout_patterns.h"
#include "native_image_patterns.h"
#include "native_event_patterns.h"
#include "native_ending_patterns.h"
struct Section {uint32_t rva,size,flags;};
struct Image {
    const unsigned char* base=nullptr;uint32_t size=0,text=0,text_size=0;
    const RUNTIME_FUNCTION* functions=nullptr;size_t count=0;std::vector<Section> sections;
    bool range(uintptr_t rva,size_t length)const{return rva<size && length<=size-rva;}
    uintptr_t wrapper(const Pattern& p,uintptr_t source,size_t call_offset)const {
        if(!source||!p.size||p.size>text_size||strlen(p.mask)!=p.size)return 0;
        uintptr_t found=0;unsigned matches=0;
        for(size_t i=0;i<=text_size-p.size;++i) {
            const auto q=base+text+i;bool same=true;
            for(size_t j=0;j<p.size;++j)if(p.mask[j]=='1'&&q[j]!=p.bytes[j]){same=false;break;}
            if(!same||!function(text+i)||call_offset+5>text_size-i)continue;
            auto owner=std::lower_bound(functions,functions+count,text+i,[](const RUNTIME_FUNCTION& f,uintptr_t value){return f.BeginAddress<value;});
            if(owner==functions+count||owner->EndAddress<text+i+call_offset+5||q[call_offset]!=0xe8)continue;
            int32_t displacement;memcpy(&displacement,q+call_offset+1,4);
            if(int64_t(text+i+call_offset+5)+displacement!=int64_t(source))continue;
            found=text+i;if(++matches>1)return 0;
        }
        return matches==1?found:0;
    }
    uintptr_t caller(uintptr_t entry,bool success)const {
        if(!entry)return 0;uintptr_t found=0;unsigned matches=0;
        for(size_t i=0;i+5<=text_size;++i){
            const auto q=base+text+i;if(q[0]!=0xe8)continue;
            int32_t displacement;memcpy(&displacement,q+1,4);
            if(int64_t(text+i+5)+displacement!=int64_t(entry))continue;
            const RUNTIME_FUNCTION* owner=nullptr;
            for(size_t j=0;j<count;++j)if(functions[j].BeginAddress<=text+i&&text+i+5<=functions[j].EndAddress){owner=functions+j;break;}
            if(!owner)continue;
            if(success){
                if(owner->BeginAddress!=text+i-12||owner->EndAddress!=text+i+10||i<12||!range(text+i-12,22))continue;
                const auto start=q-12;
                if(memcmp(start,"\x48\x83\xec\x28\xf2\x0f\x10\x0d",8)||memcmp(q+5,"\x48\x83\xc4\x28\xc3",5))continue;
                int32_t constant;memcpy(&constant,start+8,4);int64_t rva=int64_t(text+i)+constant;
                if(rva<0||!mapped(uintptr_t(rva),8))continue;
                double outcome;memcpy(&outcome,base+rva,8);if(outcome!=1.0)continue;
            }
            found=text+i;if(++matches>1)return 0;
        }
        return matches==1?found:0;
    }
    uint32_t currency(uintptr_t entry)const {
        const RUNTIME_FUNCTION* fn=nullptr;
        for(size_t i=0;i<count;++i)if(functions[i].BeginAddress==entry){fn=functions+i;break;}
        if(!fn||!range(entry,fn->EndAddress-entry)||fn->EndAddress<entry+24)return 0;
        uint32_t found=0;unsigned matches=0;
        // 已确认水晶收集器中的 native double += 1；读写必须为同一 frame 字段。
        for(uintptr_t at=entry;at+24<=fn->EndAddress;++at){
            auto q=base+at;
            if(memcmp(q,"\xf2\x0f\x10\x86",4)||memcmp(q+8,"\xf2\x0f\x58\x05",4)||memcmp(q+16,"\xf2\x0f\x11\x86",4))continue;
            uint32_t read,write;int32_t displacement;memcpy(&read,q+4,4);memcpy(&write,q+20,4);memcpy(&displacement,q+12,4);
            int64_t constant=int64_t(at+16)+displacement;
            if(read!=write||read<0x100||read>=0x400000||read%8||constant<0||!mapped(uintptr_t(constant),8))continue;
            double amount;memcpy(&amount,base+constant,8);if(amount!=1.0)continue;
            found=read;++matches;
        }
        return matches==1?found:0;
    }
    bool function(uintptr_t rva)const {
        auto first=functions,last=functions+count;
        auto found=std::lower_bound(first,last,rva,[](const RUNTIME_FUNCTION& f,uintptr_t value){return f.BeginAddress<value;});
        return found!=last && found->BeginAddress==rva && found->EndAddress>rva && range(rva,found->EndAddress-rva);
    }
    bool mapped(uintptr_t rva,size_t length=1)const {
        for(const auto& section:sections)if(rva>=section.rva && rva-section.rva<section.size && length<=section.size-(rva-section.rva))return true;
        return false;
    }
    bool load(HMODULE module) {
        base=(const unsigned char*)module;if(!base)return false;
        const auto* dos=(const IMAGE_DOS_HEADER*)base;
        if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<sizeof(*dos) || dos->e_lfanew>1024*1024)return false;
        const auto* nt=(const IMAGE_NT_HEADERS64*)(base+dos->e_lfanew);
        if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 || nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)return false;
        size=nt->OptionalHeader.SizeOfImage;
        const auto* section=IMAGE_FIRST_SECTION(nt);
        for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i,++section) {
            auto n=section->Misc.VirtualSize;
            if(!n)continue;if(!range(section->VirtualAddress,n))return false;
            sections.push_back({section->VirtualAddress,n,section->Characteristics});
            if(!memcmp(section->Name,".text",5)){text=section->VirtualAddress;text_size=n;}
            if(!memcmp(section->Name,".pdata",6)){functions=(const RUNTIME_FUNCTION*)(base+section->VirtualAddress);count=n/sizeof(*functions);}
        }
        return text&&text_size&&functions&&count;
    }
    uintptr_t resolve(const Pattern& p)const {
        if(!p.size || p.size>text_size || strlen(p.mask)!=p.size)return 0;
        size_t found=0,matches=0;
        for(size_t i=0;i<=text_size-p.size;++i) {
            if(p.mask[0]=='1'&&base[text+i]!=p.bytes[0])continue;
            bool same=true;for(size_t j=1;j<p.size;++j)if(p.mask[j]=='1'&&base[text+i+j]!=p.bytes[j]){same=false;break;}
            if(same){found=i;if(++matches>1)return 0;}
        }
        if(matches!=1 || p.instruction>=p.size)return 0;
        uintptr_t site=text+found+p.instruction;int64_t value=site;
        if(p.kind==Kind::site)return base[site]==0xe8&&range(site,5)?site:0;
        if(p.kind!=Kind::entry&&p.kind!=Kind::leaf) {
            if(p.size_operand!=4 || !range(site+p.operand,4))return 0;
            int32_t operand;memcpy(&operand,base+site+p.operand,4);
            value=p.kind==Kind::field?operand:int64_t(site+p.size_instruction)+operand;
        }
        if(value<0)return 0;
        // 某些叶函数没有 unwind 项；匹配完整代码及末尾 ret，不能仅放宽函数前缀。
        if(p.kind==Kind::leaf)return p.bytes[p.size-1]==0xc3&&p.mask[p.size-1]=='1'?uintptr_t(value):0;
        if(p.kind==Kind::field)return value>0&&value<0x400000?uintptr_t(value):0;
        if(p.kind==Kind::entry||p.kind==Kind::call)return function(uintptr_t(value))?uintptr_t(value):0;
        return mapped(uintptr_t(value),8)?uintptr_t(value):0;
    }
    uint32_t binding(const char* name)const {
        const size_t length=strlen(name)+1;uintptr_t literal=0;size_t literals=0;
        for(const auto& s:sections) {
            // 运行时 .data 可能有同名配置副本；原生代码引用的是只读常量。
            if(s.flags&(IMAGE_SCN_MEM_EXECUTE|IMAGE_SCN_MEM_WRITE) || s.size<length)continue;
            for(size_t i=0;i<=s.size-length;++i)if(base[s.rva+i]==name[0]&&!memcmp(base+s.rva+i,name,length)){literal=s.rva+i;++literals;}
        }
        if(literals!=1)return 0;
        uint32_t result=0;unsigned matches=0;
        // 原生构造器：名字的 RIP 引用，紧接字典绑定与字段地址存入 [rax]。
        for(size_t i=0;i+56<=text_size;++i) {
            const auto* q=base+text+i;
            if(q[0]!=0x48||q[1]!=0x8d||q[2]!=0x15)continue;
            int32_t disp;memcpy(&disp,q+3,4);
            if(int64_t(text+i+7)+disp!=int64_t(literal))continue;
            uint32_t field=0;unsigned local=0;
            for(size_t j=7;j+10<=56;++j) {
                if(!memcmp(q+j,"\x48\x8d\x8b",3)&&!memcmp(q+j+7,"\x48\x89\x08",3)) {
                    memcpy(&field,q+j+3,4);++local;
                }
            }
            if(local!=1 || field<0x100 || field>=0x400000 || field%8)return 0;
            result=field;++matches;
        }
        return matches==1?result:0;
    }
};
}
bool game_layout_resolve(HMODULE module,GameLayout& out) {
    out={};Image image;if(!image.load(module))return false;
    out.frame_update=image.resolve(pattern_frameUpdate);out.run_start=image.resolve(pattern_runStart);
    out.assign=image.resolve(pattern_assign);out.refresh=image.resolve(pattern_refresh);out.gambit_refresh=image.resolve(pattern_gambitRefresh);
    out.json_get=image.resolve(pattern_jsonGet);out.progress_object=image.resolve(pattern_progressObject);out.room_path=image.resolve(pattern_roomPath);
    out.checkpoint_save=image.resolve(pattern_checkpointSave);out.checkpoint_count=image.resolve(pattern_checkpointCount);out.checkpoint_object=image.resolve(pattern_checkpointObject);
    out.equipment_object=image.resolve(pattern_equipment);out.player_count=image.resolve(pattern_playerCount);
    out.run=(uint32_t)image.resolve(pattern_runField);out.save_pending=(uint32_t)image.resolve(pattern_savePending);
    out.health[0]=image.binding("player_1_life__progress");out.maximum[0]=image.binding("player_1_lifeMax__progress");
    out.health[1]=image.binding("player_2_life__progress");out.maximum[1]=image.binding("player_2_lifeMax__progress");
    out.mana=image.binding("player_1_mana__progress");out.mana_maximum=image.binding("player_1_manaMax__progress");
    out.state=out.frame_update&&out.health[0]&&out.health[1]&&out.maximum[0]&&out.maximum[1];
    out.equipment=out.equipment_object&&out.player_count&&out.assign&&out.refresh&&out.gambit_refresh;
    out.checkpoint=out.equipment&&out.json_get&&out.progress_object&&out.room_path&&out.checkpoint_save&&out.checkpoint_count&&out.checkpoint_object&&out.save_pending;
    out.events=out.run_start&&out.run;
    out.damage_entry=image.resolve(pattern_damageEntry);out.damage_site=image.resolve(pattern_damageSite);
    out.pickup_entry=image.resolve(pattern_pickupEntry);out.pickup_site=image.resolve(pattern_pickupSite);
    out.abandon_entry=image.resolve(pattern_abandonEntry);out.abandon_site=image.resolve(pattern_abandonSite);
    out.crystals=image.currency(out.pickup_entry);
    auto old_outcome=image.resolve(pattern_outcomeOld),new_outcome=image.resolve(pattern_outcomeNew);
    if(bool(old_outcome)!=bool(new_outcome)) {
        out.outcome_source=old_outcome?old_outcome:new_outcome;
        out.victory_flag=(uint32_t)image.resolve(old_outcome?pattern_outcomeOldFlag:pattern_outcomeNewFlag);
        out.defeat_entry=image.wrapper(pattern_defeatWrapper,out.outcome_source,29);
        out.victory_entry=image.wrapper(pattern_victoryWrapper,out.outcome_source,0x10c);
        out.defeat_site=image.caller(out.defeat_entry,false);
        out.victory_site=image.caller(out.victory_entry,true);
    }
    out.image_upload=image.resolve(pattern_imageUpload);out.image_load=image.resolve(pattern_imageLoad);out.image_release=image.resolve(pattern_imageRelease);
    out.image_allocate=image.resolve(pattern_imageAllocate);out.image_premultiply=image.resolve(pattern_imagePremultiply);out.image_flip=image.resolve(pattern_imageFlip);
    out.image_cache=image.resolve(pattern_imageCache);out.image_orientation=image.resolve(pattern_imageOrientation);
    out.images=out.image_upload&&out.image_load&&out.image_release&&out.image_allocate&&out.image_premultiply&&out.image_flip&&out.image_cache&&out.image_orientation;
    return true;
}
void game_layout_initialize() {
    game_layout_resolve(GetModuleHandleW(nullptr),game_layout);
    AAMOD_INFO("native layout: state=%u equipment=%u checkpoint=%u events=%u images=%u frame_update=%llx health=%x mana=%x",game_layout.state,game_layout.equipment,game_layout.checkpoint,game_layout.events,game_layout.images,(unsigned long long)game_layout.frame_update,game_layout.health[0],game_layout.mana);
}
int native_player_index(const void* metadata) {
    __try {
        if(!metadata)return -1;
        auto bytes=(const unsigned char*)metadata;
        size_t length=bytes[0]>>1;const char* name=(const char*)bytes+1;
        if(bytes[0]&1){length=*(const uint64_t*)(bytes+8);name=*(const char* const*)(bytes+16);}
        if(length!=22 || !name)return -1;
        if(!memcmp(name,"obj_player_P1_collider",22))return 0;
        return !memcmp(name,"obj_player_P2_collider",22)?1:-1;
    } __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return -1;}
}
bool native_object_named(const void* metadata,const char* expected) {
    __try {
        if(!metadata||!expected)return false;
        auto bytes=(const unsigned char*)metadata;size_t length=bytes[0]>>1;const char* name=(const char*)bytes+1;
        if(bytes[0]&1){length=*(const uint64_t*)(bytes+8);name=*(const char* const*)(bytes+16);}
        return name&&length<=128&&length==strlen(expected)&&!memcmp(name,expected,length);
    } __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return false;}
}
}
