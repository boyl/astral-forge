#include "aamod/aamod.h"
#include <string.h>
static const AAModAPI* api;
AAMOD_EXPORT uint32_t AAMOD_Init(const AAModAPI* value,uint32_t size) {
    if(!AAMOD_API_HAS(value,data_delete)||!value->data_owner)return AAMOD_ERR_ABI;
    api=value;const char preset[]={"{\"version\":1,\"name\":\"example\",\"auras\":[0,0,0,0,0]}"};
    uint32_t n=0,error=api->data_read(api->data_owner,"example-preset",0,0,&n);
    if(error==AAMOD_ERR_DATA_MISSING) {
        error=api->data_write(api->data_owner,"example-preset",preset,sizeof(preset)-1);
        if(error)return error;
    } else if(error)return error;
    char copy[256]={0};error=api->data_read(api->data_owner,"example-preset",copy,sizeof(copy)-1,&n);
    if(error)return error;
    AAMOD_LOGI(api,"data_sample: read own preset bytes=%u value=%s",n,copy);
    return AAMOD_OK;
}
AAMOD_EXPORT void AAMOD_Shutdown(void) {
    const char marker[]="shutdown-completed";
    uint32_t error=api->data_write(api->data_owner,"last-session",marker,sizeof(marker)-1);
    AAMOD_LOGI(api,"data_sample: shutdown data write=%u",error);
}
