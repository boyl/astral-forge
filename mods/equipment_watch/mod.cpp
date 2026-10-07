#include "aamod/aamod.h"
#include <windows.h>
#include <cstring>
static const AAModAPI* api;
static HANDLE stop,worker;
static DWORD WINAPI watch(void*) {
    AAModEquipment old={};old.status=UINT32_MAX;
    while(WaitForSingleObject(stop,250)==WAIT_TIMEOUT) {
        AAModEquipment value={};if(api->equipment(&value,sizeof(value)))return 1;
        if(value.status!=old.status||value.valid!=old.valid||value.scene_epoch!=old.scene_epoch||memcmp(value.auras,old.auras,sizeof(value.auras))||memcmp(value.spells,old.spells,sizeof(value.spells))) {
            AAMOD_LOGI(api,"equipment_watch: status=%u valid=%u sequence=%llu epoch=%llu auras=%u,%u,%u,%u,%u spells=%u,%u,%u,%u",value.status,value.valid,value.sequence,value.scene_epoch,value.auras[0],value.auras[1],value.auras[2],value.auras[3],value.auras[4],value.spells[0],value.spells[1],value.spells[2],value.spells[3]);old=value;
        }
    }return 0;
}
AAMOD_EXPORT uint32_t AAMOD_Init(const AAModAPI* input,uint32_t size) {
    if(!input||!AAMOD_API_HAS(input,equipment)||!input->equipment)return AAMOD_ERR_ABI;api=input;
    AAModEquipment initial={};uint32_t error=api->equipment(&initial,sizeof(initial));if(error)return error;
    AAMOD_LOGI(api,"equipment_watch: status=%u valid=%u initial",initial.status,initial.valid);
    stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!stop)return AAMOD_ERR_GENERIC;
    worker=CreateThread(nullptr,0,watch,nullptr,0,nullptr);if(!worker){CloseHandle(stop);stop=nullptr;return AAMOD_ERR_GENERIC;}return AAMOD_OK;
}
AAMOD_EXPORT void AAMOD_Shutdown(void){SetEvent(stop);WaitForSingleObject(worker,INFINITE);CloseHandle(worker);CloseHandle(stop);}
