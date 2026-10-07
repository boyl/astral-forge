#include <windows.h>
#include "aamod/aamod.h"
/* Test-only witness: incompatible declarations must block even DllMain. */
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID reserved) {
    (void)reserved;
    if(reason==DLL_PROCESS_ATTACH) {
        wchar_t path[MAX_PATH];DWORD count=GetModuleFileNameW(module,path,MAX_PATH);
        if(count && count<MAX_PATH) {
            wchar_t* last=wcsrchr(path,L'\\');if(last)wcscpy_s(last+1,MAX_PATH-(size_t)(last+1-path),L"executed.txt");
            HANDLE f=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
            if(f!=INVALID_HANDLE_VALUE)CloseHandle(f);
        }
    }
    return TRUE;
}
AAMOD_EXPORT uint32_t AAMOD_Init(const AAModAPI* api,uint32_t size) {
    (void)size;api->log(4,"GUARD_INIT_MUST_NOT_RUN");return 0;
}
