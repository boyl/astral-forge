#include "../src/core/game_layout.cpp"
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <vector>
int wmain(int argc,wchar_t** argv) {
    using namespace aamod;
    std::vector<unsigned char> bytes(0x5000);
    auto* dos=(IMAGE_DOS_HEADER*)bytes.data();dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=0x100;
    auto* nt=(IMAGE_NT_HEADERS64*)(bytes.data()+0x100);nt->Signature=IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;nt->FileHeader.NumberOfSections=3;
    nt->FileHeader.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER64);
    nt->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;nt->OptionalHeader.SizeOfImage=(DWORD)bytes.size();
    auto* sections=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<3;++i){sections[i].VirtualAddress=0x1000+i*0x1000;sections[i].Misc.VirtualSize=0x1000;}
    memcpy(sections[0].Name,".text",5);sections[0].Characteristics=IMAGE_SCN_MEM_EXECUTE;
    memcpy(sections[1].Name,".pdata",6);memcpy(sections[2].Name,".rdata",6);
    auto* functions=(RUNTIME_FUNCTION*)(bytes.data()+0x2000);functions[0].BeginAddress=0x1100;functions[0].EndAddress=0x1120;
    Image image;assert(image.load((HMODULE)bytes.data()));image.count=1;
    const unsigned char needle[]={0x48,0x89,0xc8,0xc3};const Pattern entry={needle,"1111",4,Kind::entry,0,0,0,0};
    memcpy(bytes.data()+0x1100,needle,4);assert(image.resolve(entry)==0x1100);
    // 增改与接口无关的 PE 标识不能使已验证入口失效。
    nt->FileHeader.TimeDateStamp=0x12345678;bytes[0x3f00]=0x7a;assert(image.resolve(entry)==0x1100);
    memcpy(bytes.data()+0x1200,needle,4);assert(image.resolve(entry)==0);memset(bytes.data()+0x1200,0,4);
    bytes[0x1101]=0;assert(image.resolve(entry)==0);bytes[0x1101]=0x89;
    functions[0].BeginAddress=0x10f0;assert(image.resolve(entry)==0);functions[0].BeginAddress=0x1100;
    const char name[]="player_1_life__progress";memcpy(bytes.data()+0x3000,name,sizeof(name));
    auto* code=bytes.data()+0x1300;memcpy(code,"\x48\x8d\x15",3);int32_t displacement=0x3000-0x1307;memcpy(code+3,&displacement,4);
    memcpy(code+20,"\x48\x8d\x8b",3);uint32_t field=0x201000;memcpy(code+23,&field,4);memcpy(code+27,"\x48\x89\x08",3);
    assert(image.binding(name)==field);field=0x202000;memcpy(code+23,&field,4);assert(image.binding(name)==field);
    memcpy(bytes.data()+0x3100,name,sizeof(name));assert(image.binding(name)==0);memset(bytes.data()+0x3100,0,sizeof(name));
    memcpy(bytes.data()+0x2100,name,sizeof(name));image.sections[1].flags=IMAGE_SCN_MEM_WRITE;
    assert(image.binding(name)==field);memset(bytes.data()+0x2100,0,sizeof(name));
    field=0x400000;memcpy(code+23,&field,4);assert(image.binding(name)==0);
    functions[0].EndAddress=0x1180;
    auto* increment=bytes.data()+0x1120;
    memcpy(increment,"\xf2\x0f\x10\x86",4);memcpy(increment+8,"\xf2\x0f\x58\x05",4);memcpy(increment+16,"\xf2\x0f\x11\x86",4);
    uint32_t currency=0x201008;memcpy(increment+4,&currency,4);memcpy(increment+20,&currency,4);
    int32_t constant=0x3200-0x1130;memcpy(increment+12,&constant,4);double amount=1.0;memcpy(bytes.data()+0x3200,&amount,8);
    assert(image.currency(0x1100)==currency);amount=2.0;memcpy(bytes.data()+0x3200,&amount,8);assert(image.currency(0x1100)==0);
    amount=1.0;memcpy(bytes.data()+0x3200,&amount,8);currency+=8;memcpy(increment+20,&currency,4);assert(image.currency(0x1100)==0);
    currency-=8;memcpy(increment+20,&currency,4);memcpy(bytes.data()+0x1150,increment,24);constant=0x3200-0x1160;memcpy(bytes.data()+0x115c,&constant,4);assert(image.currency(0x1100)==0);
    unsigned char metadata[32]={44};memcpy(metadata+1,"obj_player_P1_collider",22);assert(native_player_index(metadata)==0);
    memcpy(metadata+1,"obj_player_P2_collider",22);assert(native_player_index(metadata)==1);
    memcpy(metadata+1,"obj_player_P3_collider",22);assert(native_player_index(metadata)==-1);assert(native_player_index(nullptr)==-1);
    // 调用关系必须唯一，胜利调用必须实际传入 double 1；损坏边界不能定位。
    functions[1].BeginAddress=0x1700;functions[1].EndAddress=0x1720;image.count=2;
    memcpy(bytes.data()+0x1700,needle,4);bytes[0x1708]=0xe8;int32_t target=0x1100-0x170d;memcpy(bytes.data()+0x1709,&target,4);
    assert(image.wrapper(entry,0x1100,8)==0x1700);assert(image.wrapper(entry,0x1101,8)==0);
    functions[1].EndAddress=0x170c;assert(image.wrapper(entry,0x1100,8)==0);functions[1].EndAddress=0x1720;
    functions[2].BeginAddress=0x1800;functions[2].EndAddress=0x1820;image.count=3;
    memcpy(bytes.data()+0x1800,bytes.data()+0x1700,13);target=0x1100-0x180d;memcpy(bytes.data()+0x1809,&target,4);
    assert(image.wrapper(entry,0x1100,8)==0);memset(bytes.data()+0x1800,0,32);
    functions[3].BeginAddress=0x1900;functions[3].EndAddress=0x1916;image.count=4;
    memcpy(bytes.data()+0x1900,"\x48\x83\xec\x28\xf2\x0f\x10\x0d",8);int32_t constant_one=0x3300-0x190c;memcpy(bytes.data()+0x1908,&constant_one,4);
    bytes[0x190c]=0xe8;target=0x1700-0x1911;memcpy(bytes.data()+0x190d,&target,4);memcpy(bytes.data()+0x1911,"\x48\x83\xc4\x28\xc3",5);
    amount=1.0;memcpy(bytes.data()+0x3300,&amount,8);assert(image.caller(0x1700,true)==0x190c);
    amount=2.0;memcpy(bytes.data()+0x3300,&amount,8);assert(image.caller(0x1700,true)==0);amount=1.0;memcpy(bytes.data()+0x3300,&amount,8);
    bytes[0x1800]=0xe8;target=0x1700-0x1805;memcpy(bytes.data()+0x1801,&target,4);assert(image.caller(0x1700,false)==0);
    assert(native_object_named(metadata,"obj_player_P3_collider"));assert(!native_object_named(metadata,"obj_player_P1_collider"));assert(!native_object_named(nullptr,"obj_ui_spell_icon"));
    puts("layout: identity/field, ambiguity, damaged code, boundaries, native names and ending call relationships passed");
    if(argc==2) {
        HMODULE module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);assert(module);
        GameLayout layout;assert(game_layout_resolve(module,layout));
        printf("{\"state\":%u,\"equipment\":%u,\"checkpoint\":%u,\"events\":%u,\"frameUpdate\":%llu,\"health\":[%u,%u],\"maximum\":[%u,%u],\"mana\":%u,\"manaMaximum\":%u,\"run\":%u,\"savePending\":%u}\n",layout.state,layout.equipment,layout.checkpoint,layout.events,(unsigned long long)layout.frame_update,layout.health[0],layout.health[1],layout.maximum[0],layout.maximum[1],layout.mana,layout.mana_maximum,layout.run,layout.save_pending);
        assert(layout.state&&layout.equipment&&layout.checkpoint&&layout.events&&layout.images&&layout.mana&&layout.mana_maximum);
        printf("images=%u damage=%llx/%llx pickup=%llx/%llx abandon=%llx/%llx crystals=%x\n",layout.images,(unsigned long long)layout.damage_entry,(unsigned long long)layout.damage_site,(unsigned long long)layout.pickup_entry,(unsigned long long)layout.pickup_site,(unsigned long long)layout.abandon_entry,(unsigned long long)layout.abandon_site,layout.crystals);
        fflush(stdout);
        assert(layout.damage_entry&&layout.damage_site&&layout.pickup_entry&&layout.pickup_site&&layout.abandon_entry&&layout.abandon_site&&layout.crystals);
        printf("endings source=%llx defeat=%llx/%llx victory=%llx/%llx flag=%x\n",(unsigned long long)layout.outcome_source,(unsigned long long)layout.defeat_entry,(unsigned long long)layout.defeat_site,(unsigned long long)layout.victory_entry,(unsigned long long)layout.victory_site,layout.victory_flag);
        assert(layout.outcome_source&&layout.defeat_entry&&layout.defeat_site&&layout.victory_entry&&layout.victory_site&&layout.victory_flag);
        FreeLibrary(module);
    }
}
