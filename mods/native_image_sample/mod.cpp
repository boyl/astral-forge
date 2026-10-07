#include "aamod/aamod.h"
#include <windows.h>
#include <vector>
static const AAModAPI* api;
static HANDLE stopped,worker;
static int target_id,repeat_count,restore_delay;
static DWORD WINAPI run(void*) {
    int target=target_id,repeat=repeat_count,restore_seconds=restore_delay;
    uint64_t handle=0;unsigned elapsed=0,applied_at=0,cycle=0;std::vector<uint64_t> prior(71870);
    bool restoring=false,baseline=false;
    while(WaitForSingleObject(stopped,1000)==WAIT_TIMEOUT){
        ++elapsed;
        AAModGameState state={};api->game_state(&state,sizeof(state));
        if(target<0){
            if(elapsed%3)continue;
            for(uint32_t id=0;id<71870;++id){AAModNativeImageInfo info={};
                if(api->native_image_info(id,&info,sizeof(info)))continue;
                if(baseline&&state.scene_index==282&&info.upload_visits>prior[id])
                    AAMOD_LOGI(api,"native_image_sample: menu upload-visit id=%u logical=%ux%u format=%u delta=%llu",id,info.width,info.height,info.format,info.upload_visits-prior[id]);
                prior[id]=info.upload_visits;
            }
            baseline=true;continue;
        }
        if(!handle){
            AAModNativeImageInfo info={};auto info_error=api->native_image_info((uint32_t)target,&info,sizeof(info));
            if(info_error==AAMOD_ERR_NATIVE_NOT_READY)continue;
            if(info_error){AAMOD_LOGE(api,"native_image_sample: metadata error=%u",info_error);return info_error;}
            auto error=api->image_replace(api->resource_owner,(uint32_t)target,"assets/replacement.png",&handle);
            AAMOD_LOGI(api,"native_image_sample: request id=%d error=%u handle=%llu",target,error,handle);
            if(error)return error;
        }
        AAModImageReplacementStatus status={};auto error=api->image_replacement_status(api->resource_owner,handle,&status,sizeof(status));
        if(error){AAMOD_LOGE(api,"native_image_sample: status query error=%u",error);api->image_restore(api->resource_owner,handle);return error;}
        AAMOD_LOGI(api,"native_image_sample: status=%u error=%u applies=%llu restores=%llu",status.state,error?error:status.error,status.apply_count,status.restore_count);
        if(status.state==AAMOD_NATIVE_APPLIED&&!applied_at)applied_at=elapsed;
        if(!restoring&&(status.state==AAMOD_NATIVE_FAILED||(applied_at&&elapsed-applied_at>=(unsigned)restore_seconds))){api->image_restore(api->resource_owner,handle);restoring=true;}
        if(status.state==AAMOD_NATIVE_RESTORED){
            error=api->image_replacement_forget(api->resource_owner,handle);
            if(error){AAMOD_LOGE(api,"native_image_sample: forget error=%u",error);return error;}
            ++cycle;
            AAMOD_LOGI(api,"native_image_sample: cycle=%u complete",cycle);
            if(cycle>=(unsigned)repeat)return 0;
            handle=0;applied_at=0;restoring=false;
        }
    }
    if(handle)api->image_restore(api->resource_owner,handle);
    return 0;
}
AAMOD_EXPORT uint32_t AAMOD_Init(const AAModAPI* input,uint32_t size){
    if(!input||input->api_version!=AAMOD_ABI_VERSION||size<offsetof(AAModAPI,image_replacement_forget)+sizeof(input->image_replacement_forget))return AAMOD_ERR_ABI;
    api=input;if(!AAMOD_API_HAS(api,image_replacement_forget)||!(api->capabilities()&AAMOD_CAP_NATIVE_IMAGES))return AAMOD_ERR_NATIVE_UNSUPPORTED;
    target_id=api->config_int("native_image_sample.image_id",-1);
    repeat_count=api->config_int("native_image_sample.repeat_count",1);
    restore_delay=api->config_int("native_image_sample.restore_seconds",120);
    if(target_id< -1||target_id>=71870||repeat_count<1||repeat_count>16||restore_delay<1||restore_delay>3600)return AAMOD_ERR_ARGUMENT;
    AAMOD_LOGI(api,"native_image_sample: api_size=%u capabilities=%llu",api->api_size,api->capabilities());
    stopped=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!stopped)return AAMOD_ERR_GENERIC;
    worker=CreateThread(nullptr,0,run,nullptr,0,nullptr);if(!worker){CloseHandle(stopped);return AAMOD_ERR_GENERIC;}
    return AAMOD_OK;
}
AAMOD_EXPORT void AAMOD_Shutdown(void){SetEvent(stopped);WaitForSingleObject(worker,INFINITE);CloseHandle(worker);CloseHandle(stopped);}

