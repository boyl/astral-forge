#include "aamod/aamod.h"
#include <windows.h>
static const AAModAPI* api;static HANDLE stop,worker;
static DWORD WINAPI run(void*) {
    uint64_t cursor=0;
    while(WaitForSingleObject(stop,50)==WAIT_TIMEOUT) {
        AAModGameplayEvent events[16]={};AAModGameplayInfo info={};uint32_t count=0;
        auto error=api->gameplay_events(cursor,events,16,sizeof(events[0]),&count,&info,sizeof(info));
        if(error==AAMOD_ERR_EVENT_CURSOR){AAMOD_LOGW(api,"native_event_watch: lost history oldest=%llu newest=%llu",(unsigned long long)info.oldest,(unsigned long long)info.newest);cursor=info.newest;continue;}
        if(error){AAMOD_LOGE(api,"native_event_watch: error=%u",error);return 1;}
        for(unsigned i=0;i<count;++i) {auto& event=events[i];cursor=event.id;AAMOD_LOGI(api,"native_event_watch: id=%llu kind=%u source=%x thread=%u run=%.0f supported=%u before=%.3f after=%.3f amount=%.3f content_kind=%u content_id=%u",(unsigned long long)event.id,event.kind,event.source_rva,event.update_thread_id,event.run_key,info.supported_kinds,event.before,event.after,event.amount,event.content_kind,event.content_id);}
    }return 0;
}
AAMOD_EXPORT uint32_t AAMOD_Init(const AAModAPI* value,uint32_t size) {
    if(!value||size<328||value->api_version!=1)return AAMOD_ERR_ABI;api=value;
    stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!stop)return AAMOD_ERR_GENERIC;
    worker=CreateThread(nullptr,0,run,nullptr,0,nullptr);if(!worker){CloseHandle(stop);stop=nullptr;return AAMOD_ERR_GENERIC;}return 0;
}
AAMOD_EXPORT void AAMOD_Shutdown() {SetEvent(stop);WaitForSingleObject(worker,INFINITE);CloseHandle(worker);CloseHandle(stop);worker=stop=nullptr;}
