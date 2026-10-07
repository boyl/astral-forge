#undef NDEBUG
#include <windows.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include <intrin.h>
#include "../src/core/gameplay_events.h"
#include "../src/core/game_profile.h"
#include "../src/core/game_state.h"
#include "../src/core/hook.h"
static unsigned char* image;static unsigned char* frame;
static double loss=20;static size_t caller_rva=0x311fbf6;static unsigned damage_calls;static bool multiplayer,ready=true;
static void original_run(void* value){*(double*)((unsigned char*)value+0x1c4768)+=1;}
static void original_hit(void* value){++damage_calls;*(double*)((unsigned char*)value+0x1c4ff0)-=loss;}
static void* expected_player;static void* expected_collectible;static double gain=1;static unsigned pickup_calls;
static void original_collect(void* value,void* player,void* collectible){assert(player==expected_player&&collectible==expected_collectible);++pickup_calls;*(double*)((unsigned char*)value+0x1c3f30)+=gain;}
static unsigned abandon_calls;static void original_abandoned(void*){++abandon_calls;}
static double original_end(void*){return 17.25;}
static double original_win(void*,double outcome){return 17.25+outcome;}
static void* fake_return_address(){return image+caller_rva;}
static HMODULE fake_module(LPCWSTR){return (HMODULE)image;}
static DWORD fake_environment(LPCSTR,LPSTR text,DWORD){strcpy(text,"1");return 1;}
namespace aamod {
uint32_t game_profile_query(AAModGameInfo* info,uint32_t){*info={};info->identity_status=AAMOD_GAME_IDENTITY_MATCH;return 0;}
uint32_t game_state_query(AAModGameState* state,uint32_t){*state={};state->status=ready?AAMOD_STATE_READY:AAMOD_STATE_WAITING;state->scene_index=268;state->players[0].present=1;state->players[1].present=multiplayer;return 0;}
namespace hook {
bool install(void* target,void*,void** original,size_t){if(target==image+0x31ddff0)*original=(void*)original_run;else if(target==image+0x14b8a20)*original=(void*)original_hit;else if(target==image+0x35057b0)*original=(void*)original_win;else if(target==image+0x15a8b80)*original=(void*)original_abandoned;else if(target==image+0x3505770)*original=(void*)original_end;else if(target==image+0x200ae00)*original=(void*)original_collect;else return false;return true;}
bool remove(void*){return true;}
}
}
#define _ReturnAddress fake_return_address
#define GetModuleHandleW fake_module
#define GetEnvironmentVariableA fake_environment
#include "../src/core/native_events.cpp"
#undef _ReturnAddress
#undef GetModuleHandleW
#undef GetEnvironmentVariableA
int main() {
    using namespace aamod;
    image=(unsigned char*)VirtualAlloc(nullptr,0x3510000,MEM_RESERVE,PAGE_READWRITE);assert(image);
    assert(VirtualAlloc(image+0x31dd000,0x1000,MEM_COMMIT,PAGE_READWRITE));assert(VirtualAlloc(image+0x14b8000,0x1000,MEM_COMMIT,PAGE_READWRITE));
    const unsigned char run_prefix[]={0x48,0x8b,0xc4,0x56,0x48,0x81,0xec,0xb0,0,0,0,0x0f,0x29,0x78,0xd8};memcpy(image+0x31ddff0,run_prefix,sizeof(run_prefix));
    const unsigned char hit_prefix[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x8b,0x99,0xc0,0,0,0};memcpy(image+0x14b8a20,hit_prefix,sizeof(hit_prefix));
    assert(VirtualAlloc(image+0x311f000,0x1000,MEM_COMMIT,PAGE_READWRITE));
    const unsigned char call[]={0xe8,0x2a,0x8e,0x39,0xfe};memcpy(image+0x311fbf1,call,sizeof(call));
    assert(VirtualAlloc(image+0x3505000,0x1000,MEM_COMMIT,PAGE_READWRITE));assert(VirtualAlloc(image+0x1dfd000,0x1000,MEM_COMMIT,PAGE_READWRITE));
    const unsigned char end_prefix[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x8b,0x99,0x18,0x07,0,0};memcpy(image+0x3505770,end_prefix,sizeof(end_prefix));
    const unsigned char end_call[]={0xe8,0xcc,0x80,0x70,0x01};memcpy(image+0x1dfd69f,end_call,sizeof(end_call));
    assert(VirtualAlloc(image+0x200a000,0x1000,MEM_COMMIT,PAGE_READWRITE));
    const unsigned char pickup_prefix[]={0x48,0x8b,0xc4,0x4c,0x89,0x40,0x18,0x48,0x89,0x50,0x10,0x55,0x53,0x56,0x57};memcpy(image+0x200ae00,pickup_prefix,sizeof(pickup_prefix));
    const unsigned char pickup_call[]={0xe8,0x1e,0,0,0};memcpy(image+0x200addd,pickup_call,sizeof(pickup_call));
    assert(VirtualAlloc(image+0x1368000,0x1000,MEM_COMMIT,PAGE_READWRITE));
    const unsigned char win_prefix[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};memcpy(image+0x35057b0,win_prefix,sizeof(win_prefix));
    const unsigned char win_call[]={0xe8,0xef,0xd6,0x19,0x02};memcpy(image+0x13680bc,win_call,sizeof(win_call));
    assert(VirtualAlloc(image+0x15a8000,0x1000,MEM_COMMIT,PAGE_READWRITE));assert(VirtualAlloc(image+0x1d5e000,0x1000,MEM_COMMIT,PAGE_READWRITE));
    const unsigned char abandon_prefix[]={0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};memcpy(image+0x15a8b80,abandon_prefix,sizeof(abandon_prefix));
    const unsigned char abandon_call[]={0xe8,0xe1,0x9d,0x84,0xff};memcpy(image+0x1d5ed9a,abandon_call,sizeof(abandon_call));
    frame=(unsigned char*)VirtualAlloc(nullptr,0x1d0000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(frame);
    auto& health=*(double*)(frame+0x1c4ff0);auto& selected=*(double*)(frame+0x728);selected=1;health=100;*(uint32_t*)(frame+0x720)=3;
    native_events_initialize();assert(native_events_available());start(frame);damage(frame);
    AAModGameplayEvent records[8]={};AAModGameplayInfo info={};uint32_t count=0;
    assert(gameplay_events_query(0,records,8,sizeof(records[0]),&count,&info,sizeof(info))==0&&count==2&&info.supported_kinds==15);
    assert(records[0].kind==1&&records[0].source_rva==0x31ddff0&&records[0].run_key==1);
    assert(records[1].kind==2&&records[1].source_rva==0x311fbf1&&records[1].before==100&&records[1].after==80&&records[1].amount==20&&records[1].update_thread_id==GetCurrentThreadId());

    uint64_t cursor=info.newest;
    unsigned char player[16]={},collectible[16]={};*(void**)(player+8)=image+0xc407b80;expected_player=player;expected_collectible=collectible;
    auto& crystals=*(double*)(frame+0x1c3f30);crystals=150;caller_rva=0x200ade2;pickup(frame,player,collectible);
    assert(gameplay_events_query(cursor,records,8,sizeof(records[0]),&count,&info,sizeof(info))==0&&count==1&&records[0].kind==4&&records[0].source_rva==0x200ae00&&records[0].before==150&&records[0].after==151&&records[0].amount==1&&records[0].content_kind==3&&records[0].content_id==1);
    cursor=info.newest;gain=0;pickup(frame,player,collectible);gain=-1;pickup(frame,player,collectible);gain=1;
    caller_rva=0x1234;pickup(frame,player,collectible);caller_rva=0x200ade2;
    multiplayer=true;pickup(frame,player,collectible);multiplayer=false;ready=false;pickup(frame,player,collectible);ready=true;
    *(void**)(player+8)=nullptr;pickup(frame,player,collectible);*(void**)(player+8)=image+0xc407b80;
    assert(gameplay_events_query(cursor,records,8,sizeof(records[0]),&count,&info,sizeof(info))==0&&count==0&&pickup_calls==7);
    caller_rva=0x311fbf6;
    caller_rva=0x143f79b;damage(frame);caller_rva=0x311fbf6;health=80; // Rounding/stat callers never publish damage.
    loss=-10;damage(frame);assert(health==90); // native healing is not damage.
    loss=10;selected=2;damage(frame);selected=1;
    multiplayer=true;damage(frame);multiplayer=false;
    ready=false;damage(frame);ready=true;
    health=std::numeric_limits<double>::quiet_NaN();damage(frame);health=0;damage(frame);
    assert(damage_calls==8);assert(gameplay_events_query(cursor,records,8,sizeof(records[0]),&count,&info,sizeof(info))==0&&count==0);
    caller_rva=0x1dfd6a4;health=10;assert(defeat(frame)==17.25);health=0;multiplayer=true;assert(defeat(frame)==17.25);multiplayer=false;
    caller_rva=0x1234;assert(defeat(frame)==17.25);caller_rva=0x1dfd6a4;
    assert(gameplay_events_query(cursor,records,8,sizeof(records[0]),&count,&info,sizeof(info))==0&&count==0);
    assert(defeat(frame)==17.25);assert(defeat(frame)==17.25);
    assert(gameplay_events_query(cursor,records,8,sizeof(records[0]),&count,&info,sizeof(info))==0&&count==1&&records[0].kind==8&&records[0].source_rva==0x3505770&&records[0].run_key==1);
    cursor=info.newest;
    caller_rva=0x13680c1;*(double*)(frame+0x1c4768)=2;health=100;*(unsigned char*)(frame+0x1c14c2)=0;assert(victory(frame,1)==18.25);
    *(unsigned char*)(frame+0x1c14c2)=4;assert(victory(frame,0)==17.25);multiplayer=true;assert(victory(frame,1)==18.25);multiplayer=false;
    assert(gameplay_events_query(cursor,records,8,sizeof(records[0]),&count,&info,sizeof(info))==0&&count==0);
    assert(victory(frame,1)==18.25);assert(victory(frame,1)==18.25);
    assert(gameplay_events_query(cursor,records,8,sizeof(records[0]),&count,&info,sizeof(info))==0&&count==1&&records[0].kind==8&&records[0].source_rva==0x35057b0&&records[0].content_kind==4&&records[0].content_id==1);
    cursor=info.newest;*(double*)(frame+0x1c4768)=3;caller_rva=0x31117e6;abandon(frame); // Shared defeat source is excluded.
    caller_rva=0x1d5ed9f;multiplayer=true;abandon(frame);multiplayer=false;ready=false;abandon(frame);ready=true;
    assert(gameplay_events_query(cursor,records,8,sizeof(records[0]),&count,&info,sizeof(info))==0&&count==0);
    abandon(frame);abandon(frame);assert(abandon_calls==5);
    assert(gameplay_events_query(cursor,records,8,sizeof(records[0]),&count,&info,sizeof(info))==0&&count==1&&records[0].kind==8&&records[0].source_rva==0x15a8b80&&records[0].content_kind==4&&records[0].content_id==2);
    cursor=info.newest;caller_rva=0x311fbf6;
    native_events_shutdown();assert(!native_events_available());health=100;damage(frame);assert(health==90); // in-flight forwarding remains valid after stop.
    assert(gameplay_events_query(cursor,records,8,sizeof(records[0]),&count,&info,sizeof(info))==0&&count==0&&info.status==AAMOD_STATE_STOPPED);
    health=0;assert(defeat(frame)==17.25);pickup(frame,player,collectible);assert(pickup_calls==8);
    VirtualFree(frame,0,MEM_RELEASE);VirtualFree(image,0,MEM_RELEASE);
    puts("native callbacks: frame ABI, source/delta fields, heal/P2/multiplayer/not-ready/NaN/dead exclusion and stopped forwarding passed");
}

