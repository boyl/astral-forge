#undef NDEBUG
#include <cassert>
#include <cstdio>
#include "../src/core/gameplay_events.h"
int main() {
    using namespace aamod;AAModGameplayEvent values[3]={};AAModGameplayInfo info={};uint32_t count=99;
    assert(gameplay_events_query(0,values,3,sizeof(values[0]),&count,&info,sizeof(info))==0&&count==0&&info.status==AAMOD_STATE_UNSUPPORTED);
    gameplay_events_status(AAMOD_STATE_READY,AAMOD_GAMEPLAY_RUN_START);
    AAModGameplayEvent event={};event.kind=AAMOD_GAMEPLAY_DAMAGE;gameplay_events_record(event);
    event.kind=AAMOD_GAMEPLAY_RUN_START;event.source_rva=0x31ddff0;event.run_key=7;gameplay_events_record(event);event.run_key=8;gameplay_events_record(event);
    assert(gameplay_events_query(0,values,1,sizeof(values[0]),&count,&info,sizeof(info))==0&&count==1&&values[0].id==1&&values[0].run_key==7&&values[0].source_rva==0x31ddff0&&info.newest==2);
    assert(gameplay_events_query(1,values,3,sizeof(values[0]),&count,&info,sizeof(info))==0&&count==1&&values[0].id==2&&values[0].run_key==8);
    for(unsigned i=0;i<130;++i)gameplay_events_record(event);
    values[0].id=999;
    assert(gameplay_events_query(1,values,3,sizeof(values[0]),&count,&info,sizeof(info))==AAMOD_ERR_EVENT_CURSOR&&count==0&&info.oldest==5&&info.newest==132&&values[0].id==999);
    assert(gameplay_events_query(133,values,3,sizeof(values[0]),&count,&info,sizeof(info))==AAMOD_ERR_EVENT_CURSOR&&count==0);
    assert(gameplay_events_query(0,values,3,sizeof(values[0]),&count,&info,sizeof(info))==0&&count==3&&values[0].id==5&&values[2].id==7);
    assert(gameplay_events_query(0,nullptr,0,sizeof(values[0]),&count,&info,sizeof(info))==0&&count==0&&info.newest==132);
    count=77;assert(gameplay_events_query(0,values,3,1,&count,&info,sizeof(info))==AAMOD_ERR_ARGUMENT&&count==77);
    gameplay_events_status(AAMOD_STATE_STOPPED,0);gameplay_events_record(event);
    assert(gameplay_events_query(132,values,3,sizeof(values[0]),&count,&info,sizeof(info))==0&&count==0&&info.status==AAMOD_STATE_STOPPED&&info.newest==132);
    puts("native event stream: source records, supported kinds, paging, overflow, invalid buffers and stopped lifecycle passed");
}
