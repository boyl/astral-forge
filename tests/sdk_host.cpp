/* Independent SDK smoke host: loader only, no GPU, no game and no current SDK header. */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#pragma comment(lib,"winmm.lib")
int main() {
    setvbuf(stdout,nullptr,_IONBF,0);
    timeGetTime();
    HMODULE core=nullptr;
    ULONGLONG deadline=GetTickCount64()+15000;
    unsigned (*ready)()=nullptr;
    do {
        core=GetModuleHandleW(L"aamod_core.dll");
        if(core)ready=(unsigned(*)())GetProcAddress(core,"AAMOD_CoreReady");
        if(ready&&ready())break;
        Sleep(10);
    } while(GetTickCount64()<deadline);
    if(!core){puts("FAIL: core not loaded by winmm");return 1;}
    auto wait=(void(*)(unsigned))GetProcAddress(core,"AAMOD_WaitForCore");
    auto shutdown=(void(*)())GetProcAddress(core,"AAMOD_ShutdownCore");
    auto hooks=(size_t(*)())GetProcAddress(core,"AAMOD_ActiveHookCount");
    auto frames=(size_t(*)())GetProcAddress(core,"AAMOD_SubscriberCount");
    auto images=(size_t(*)())GetProcAddress(core,"AAMOD_ImageCount");
    if(!wait||!ready||!shutdown||!hooks||!frames||!images)return 2;
    wait(15000);if(!ready()){puts("FAIL: bootstrap timeout");return 3;}
    shutdown();shutdown();
    if(hooks()||frames()||images()){puts("FAIL: owned resources remain");return 4;}
    puts("RESULT=OK: loader ready and repeated shutdown clean; game adapter unsupported by design");return 0;
}
