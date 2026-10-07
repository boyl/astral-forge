#include "gameplay_events.h"
#include <windows.h>
namespace aamod {
namespace {SRWLOCK lock=SRWLOCK_INIT;AAModGameplayEvent events[128]={};AAModGameplayInfo current={sizeof(current),AAMOD_STATE_UNSUPPORTED,0,0,0,0};}
void gameplay_events_status(uint32_t status,uint32_t supported) {
    AcquireSRWLockExclusive(&lock);current.status=status;current.supported_kinds=supported;ReleaseSRWLockExclusive(&lock);
}
void gameplay_events_record(AAModGameplayEvent event) {
    AcquireSRWLockExclusive(&lock);
    if(current.status==AAMOD_STATE_READY&&(event.kind&current.supported_kinds)==event.kind&&event.kind) {
        event.id=++current.newest;current.oldest=current.newest>128?current.newest-127:1;events[(event.id-1)%128]=event;
    }ReleaseSRWLockExclusive(&lock);
}
uint32_t gameplay_events_query(uint64_t after,AAModGameplayEvent* output,uint32_t capacity,
    uint32_t event_size,uint32_t* count,AAModGameplayInfo* info,uint32_t info_size) {
    if(!count||!info||info_size<sizeof(*info)||event_size!=sizeof(AAModGameplayEvent)||(capacity&&!output))return AAMOD_ERR_ARGUMENT;
    AcquireSRWLockShared(&lock);*count=0;*info=current;
    if(after>current.newest||(after&&current.oldest&&after<current.oldest-1)){ReleaseSRWLockShared(&lock);return AAMOD_ERR_EVENT_CURSOR;}
    for(uint64_t id=after?after+1:current.oldest;id&&id<=current.newest&&*count<capacity;++id)output[(*count)++]=events[(id-1)%128];
    ReleaseSRWLockShared(&lock);return 0;
}
}
