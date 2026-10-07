#include "../src/core/hook.cpp"
#include <cstdio>
#include <cstdlib>
#include <atomic>
static int hits;
static int(*original)(int);
static int detour(int input){++hits;return original(input)+100;}
static int(*concurrent_original)(int);
static std::atomic<bool> running{false},bad_result{false};
static std::atomic<unsigned> concurrent_calls{0};
static int concurrent_detour(int input){auto function=(int(*)(int))InterlockedCompareExchangePointer((void*volatile*)&concurrent_original,nullptr,nullptr);return function(input)+100;}
static DWORD WINAPI caller(void* code){auto function=(int(*)(int))code;while(running.load()){int result=function(20);if(result!=21&&result!=121)bad_result.store(true);++concurrent_calls;}return 0;}
static void require(bool value,const char* message){if(!value){fprintf(stderr,"FAIL: %s\n",message);exit(1);}}
int main(){
    using namespace aamod::hook;
    // Exhaust opcode and REX prefixes independently of the production tables.
    for(unsigned opcode=0x50;opcode<=0x5f;++opcode){
        for(unsigned prefix=0;prefix<=17;++prefix){
            uint8_t bytes[3]={(uint8_t)opcode,0x90,0x90};size_t length=1;
            if(prefix){bytes[0]=prefix==17?0x66:(uint8_t)(0x3f+prefix);bytes[1]=(uint8_t)opcode;length=2;}
            Insn instruction={};require(decode_insn(bytes,bytes+length,&instruction),"register stack instruction refused");
            require(instruction.len==length&&instruction.fix_off==kNoFix&&!instruction.bad_rel8,"register stack length/relocation");
            require(!decode_insn(bytes,bytes+length-1,&instruction),"truncated stack instruction accepted");
        }
    }
    // The observed native image loader sequence must decode to complete instructions.
    const uint8_t prologue[]={0x40,0x56,0x48,0x83,0xec,0x30,0x0f,0xb7,0x41,0x04,0x48,0x8b,0xf1,0x66,0x83,0xc8,0x01};
    const unsigned lengths[]={2,4,4,3,4};size_t offset=0;
    for(auto length:lengths){Insn i={};require(decode_insn(prologue+offset,prologue+sizeof(prologue),&i)&&i.len==length,"native image prologue regression");offset+=length;}
    for(uint8_t opcode:{uint8_t(0x60),uint8_t(0x61),uint8_t(0xc4),uint8_t(0xc5)}){
        uint8_t bytes[]={opcode,0,0,0};Insn i={};require(!decode_insn(bytes,bytes+sizeof(bytes),&i),"unsupported opcode accepted");
    }
    // Execute balanced native-code functions, hook them, then verify restoration.
    for(unsigned prefix:{0u,0x40u,0x41u,0x48u,0x49u,0x66u}){
        for(unsigned reg=0;reg<8;++reg){
            auto code=(uint8_t*)VirtualAlloc(nullptr,64,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);require(code!=nullptr,"allocate function");
            memset(code,0x90,64);size_t cursor=0;
            if(prefix)code[cursor++]=(uint8_t)prefix;code[cursor++]=(uint8_t)(0x50+reg);
            if(prefix)code[cursor++]=(uint8_t)prefix;code[cursor++]=(uint8_t)(0x58+reg);
            code[cursor++]=0x8d;code[cursor++]=0x41;code[cursor++]=1;code[32]=0xc3;
            uint8_t saved[64];memcpy(saved,code,64);DWORD protection;
            require(VirtualProtect(code,64,PAGE_EXECUTE_READ,&protection)!=0,"protect function");FlushInstructionCache(GetCurrentProcess(),code,64);
            auto function=(int(*)(int))code;require(function(20)==21,"original native function");
            void* trampoline=nullptr;require(install(code,(void*)detour,&trampoline),"hook stack prologue");original=(int(*)(int))trampoline;
            int prior_hits=hits;require(function(20)==121&&hits==prior_hits+1,"detour and relocated original effect");
            require(remove(code)&&function(20)==21,"remove hook");require(memcmp(saved,code,64)==0,"restore exact original bytes");
            VirtualFree(code,0,MEM_RELEASE);
        }
    }
    // Short conditional branches stay untouched beyond the five-byte prefix.
    for(unsigned input:{0u,1u}) {
        uint8_t bytes[64];memset(bytes,0x90,sizeof(bytes));
        const uint8_t body[]={0x40,0x53,0x48,0x83,0xec,0x20,0x85,0xc9,0x75,0x0b,0xb8,0x0a,0,0,0,0x48,0x83,0xc4,0x20,0x5b,0xc3,0xb8,0x14,0,0,0,0x48,0x83,0xc4,0x20,0x5b,0xc3};
        memcpy(bytes,body,sizeof(body));auto code=(uint8_t*)VirtualAlloc(nullptr,64,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        memcpy(code,bytes,64);DWORD protection;VirtualProtect(code,64,PAGE_EXECUTE_READ,&protection);
        auto function=(int(*)(int))code;int expected=input?20:10;require(function(input)==expected,"branch original");
        void* trampoline=nullptr;require(!install(code,(void*)detour,&trampoline),"absolute patch must refuse rel8");
        require(install_near(code,(void*)detour,&trampoline),"near relay patch");original=(int(*)(int))trampoline;
        require(function(input)==expected+100,"near relay both branch outcomes");
        require(remove(code)&&memcmp(code,bytes,64)==0&&function(input)==expected,"near relay restore");VirtualFree(code,0,MEM_RELEASE);
    }
    auto code=(uint8_t*)VirtualAlloc(nullptr,64,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);require(code!=nullptr,"concurrent allocate");memset(code,0x90,64);code[0]=0x8d;code[1]=0x41;code[2]=1;code[32]=0xc3;DWORD protection;VirtualProtect(code,64,PAGE_EXECUTE_READ,&protection);
    running.store(true);HANDLE thread=CreateThread(nullptr,0,caller,code,0,nullptr);require(thread!=nullptr,"concurrent caller thread");
    while(concurrent_calls.load()<100)Sleep(1);
    for(unsigned cycle=0;cycle<64;++cycle){require(install(code,(void*)concurrent_detour,(void**)&concurrent_original),"concurrent install");Sleep(1);require(remove(code),"concurrent remove");Sleep(1);}
    running.store(false);require(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0,"concurrent caller stopped");CloseHandle(thread);require(!bad_result.load()&&concurrent_calls.load()>100,"concurrent results or return path damaged");VirtualFree(code,0,MEM_RELEASE);
    require(active()==0,"hooks remain after tests");puts("hook contracts passed (48 stack variants, near-relay branch outcomes and 64 concurrent install/remove cycles)");
}
