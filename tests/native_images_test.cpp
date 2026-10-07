#include "../src/core/native_images.cpp"
#include "../src/core/hook.cpp"
#include <cstdio>
#include <cstdlib>
#include <thread>
static void require(bool value,const char* text){if(!value){fprintf(stderr,"FAIL native images: %s\n",text);exit(1);}}
static unsigned allocations,frees,loads,texture_id=100,fail_upload;
static bool fail_allocation;
static std::vector<uint8_t> uploaded;
static void* allocate(size_t size){if(fail_allocation)return nullptr;++allocations;return malloc(size);}
static void release(void* object){auto p=(uint8_t*)object;void* pixels=*(void**)(p+0x30);if(pixels){free(pixels);++frees;}*(void**)(p+0x30)=nullptr;*(uint32_t*)(p+0x28)=0;}
static void load(void* object){++loads;auto p=(uint8_t*)object;*(uint32_t*)(p+0x40)=2;*(void**)(p+0x30)=allocate(16);memset(*(void**)(p+0x30),0x33,16);}
static void upload(void* object){auto p=(uint8_t*)object;if(*(uint32_t*)(p+0x28))return;void* pixels=*(void**)(p+0x30);if(!pixels)return;
    uploaded.assign((uint8_t*)pixels,(uint8_t*)pixels+16);if(fail_upload)--fail_upload;else *(uint32_t*)(p+0x28)=++texture_id;
    free(pixels);++frees;*(void**)(p+0x30)=nullptr;
}
static void premultiply(void* pixels,int width,int height){auto p=(uint8_t*)pixels;for(int i=0;i<width*height;++i,p+=4)for(int c=0;c<3;++c)p[c]=uint8_t((unsigned(p[c])*(unsigned(p[3])+1))>>8);}
static void flip(void* pixels,int width,int height,int stride){auto p=(uint8_t*)pixels;for(int y=0;y<height/2;++y)for(int x=0;x<width*stride;++x)std::swap(p[y*width*stride+x],p[(height-1-y)*width*stride+x]);}
namespace aamod {
uint32_t game_profile_query(AAModGameInfo* info,uint32_t){*info={};return 0;}
uint32_t resources_image_copy(uint64_t owner,const char* path,std::vector<uint8_t>& pixels,uint32_t& width,uint32_t& height){
    if(owner<1||owner>4)return AAMOD_ERR_RESOURCE_HANDLE;
    if(!strcmp(path,"bad"))return AAMOD_ERR_RESOURCE_FORMAT;
    width=!strcmp(path,"wrong")?3:2;height=2;
    pixels={255,0,0,255,0,255,0,255,0,0,255,128,255,255,0,128};return 0;
}
}
int main(){
    using namespace aamod;
    struct Cache {uint8_t padding[24];void* entries[71870];};static Cache cache={};
    base=(unsigned char*)cache.entries-0xa314390;
    unsigned char object[0x48]={};*(uint32_t*)object=7;*(uint16_t*)(object+0x18)=2;*(uint16_t*)(object+0x1a)=2;
    *(uint16_t*)(object+0x1c)=4;*(uint16_t*)(object+0x1e)=4;*(uint16_t*)(object+0x20)=1;*(uint16_t*)(object+0x22)=1;*(uint16_t*)(object+0x24)=1;*(uint16_t*)(object+0x26)=1;
    *(uint32_t*)(object+0x28)=55;*(uint32_t*)(object+0x40)=2;cache.entries[7]=object;
    upload_original=::upload;load_original=::load;release_original=::release;allocate_native=::allocate;premultiply_native=::premultiply;flip_native=::flip;
    AAModNativeImageInfo info={};uint64_t handle=777;
    require(native_image_info(7,&info,sizeof(info))==AAMOD_ERR_NATIVE_UNSUPPORTED,"unknown profile unavailable");available=true;
    require(native_image_info(7,&info,sizeof(info))==AAMOD_ERR_NATIVE_NOT_READY,"unobserved image unavailable");
    aamod::upload(object);require(native_image_info(7,&info,sizeof(info))==0&&info.width==2&&info.upload_visits==1,"coherent metadata");
    uint64_t initializing=0;require(native_image_replace(3,7,"good",&initializing)==0,"initialization queues request");
    aamod::upload(object);require(*(uint32_t*)(object+0x28)==55,"no mutation before initialization commit");
    native_images_revoke(3);require(!bindings.count(7)&&!replacements.count(initializing),"failed initialization cancels pending payload");
    native_images_activate(1);native_images_activate(2);
    require(native_image_replace(1,7,"wrong",&handle)==AAMOD_ERR_NATIVE_DIMENSIONS&&handle==777,"size mismatch no handle side effect");
    require(native_image_replace(1,7,"bad",&handle)==AAMOD_ERR_RESOURCE_FORMAT,"decode failure");
    require(native_image_replace(99,7,"good",&handle)==AAMOD_ERR_RESOURCE_HANDLE,"unknown owner");
    require(native_image_replace(1,7,"good",&handle)==0,"queue replacement");
    uint64_t other=888;require(native_image_replace(2,7,"good",&other)==AAMOD_ERR_NATIVE_CONFLICT&&other==888,"different owner conflict");
    AAModImageReplacementStatus status={};require(native_image_status(2,handle,&status,sizeof(status))==AAMOD_ERR_RESOURCE_HANDLE,"status isolation");
    require(native_image_restore(2,handle)==AAMOD_ERR_RESOURCE_HANDLE,"restore isolation");
    require(native_image_forget(1,handle)==AAMOD_ERR_NATIVE_PENDING,"cannot forget live mutation");
    aamod::upload(object);native_image_status(1,handle,&status,sizeof(status));require(status.state==AAMOD_NATIVE_APPLIED&&status.apply_count==1,"render applies queued request");
    require(uploaded[0]==255&&uploaded[3]==255&&uploaded[10]==128&&uploaded[11]==128,"independent straight-alpha to native premultiplication");
    require(*(uint16_t*)(object+0x1c)==2&&*(uint16_t*)(object+0x20)==0&&*(uint16_t*)(object+0x24)==2,"full logical replacement replaces padded/cropped buffer");
    native_image_info(7,&info,sizeof(info));require(info.format==0&&info.has_texture,"metadata reflects applied texture");
    auto count=allocations;for(int i=0;i<200;++i)aamod::upload(object);require(allocations==count,"cached draws allocate nothing");
    ::release(object);::load(object);aamod::upload(object);native_image_status(1,handle,&status,sizeof(status));require(status.apply_count==2,"cache reload reapplies replacement");
    require(native_image_restore(1,handle)==0,"request restoration");aamod::upload(object);native_image_status(1,handle,&status,sizeof(status));
    require(status.state==AAMOD_NATIVE_RESTORED&&status.restore_count==1&&*(uint32_t*)(object+0x40)==2&&uploaded[0]==0x33,"native original restored");
    require(*(uint16_t*)(object+0x1c)==4&&*(uint16_t*)(object+0x20)==1&&*(uint16_t*)(object+0x24)==1,"original padding/crop restored");
    require(native_image_restore(1,handle)==0,"idempotent restore");
    native_image_info(7,&info,sizeof(info));require(info.format==2&&info.has_texture,"metadata reflects restored original");
    uint64_t cancelled=0;native_image_replace(1,7,"good",&cancelled);native_image_restore(1,cancelled);native_image_status(1,cancelled,&status,sizeof(status));require(status.state==AAMOD_NATIVE_RESTORED,"cancel before first draw");
    native_images_activate(4);uint64_t old_status=0;native_image_replace(4,7,"good",&old_status);aamod::upload(object);native_image_restore(4,old_status);aamod::upload(object);
    uint64_t revoked=0;native_image_replace(2,7,"good",&revoked);aamod::upload(object);
    native_images_revoke(4);require(bindings.count(7)&&bindings.find(7)->second==revoked,"cleanup old status cannot erase a new owner's binding");native_images_revoke(2);
    require(native_image_status(2,revoked,&status,sizeof(status))==AAMOD_ERR_RESOURCE_HANDLE,"revoked owner stops immediately");
    aamod::upload(object);require(!bindings.count(7)&&!replacements.count(revoked),"owner unload renderer restores and releases retained payload");
    require(native_image_replace(2,7,"good",&other)==AAMOD_ERR_RESOURCE_HANDLE,"no registration after owner revoke");
    *(uint32_t*)(base+0xa314378)=1;uint64_t flipped=0;native_image_replace(1,7,"good",&flipped);aamod::upload(object);
    require(uploaded[2]==128&&uploaded[3]==128&&uploaded[8]==255&&uploaded[11]==255,"backend row orientation");native_image_restore(1,flipped);aamod::upload(object);
    fail_allocation=true;uint64_t failed=0;native_image_replace(1,7,"good",&failed);aamod::upload(object);native_image_status(1,failed,&status,sizeof(status));
    require(status.state==AAMOD_NATIVE_FAILED&&status.error==AAMOD_ERR_RESOURCE_MEMORY&&*(uint32_t*)(object+0x40)==2,"allocation failure preserves original");fail_allocation=false;native_image_restore(1,failed);
    fail_upload=1;uint64_t failed_gpu=0;native_image_replace(1,7,"good",&failed_gpu);aamod::upload(object);native_image_status(1,failed_gpu,&status,sizeof(status));
    require(status.state==AAMOD_NATIVE_FAILED&&*(uint32_t*)(object+0x40)==2,"upload failure restores original");native_image_restore(1,failed_gpu);aamod::upload(object);
    uint64_t restore_failed=0;native_image_replace(1,7,"good",&restore_failed);aamod::upload(object);
    fail_upload=1;native_image_restore(1,restore_failed);aamod::upload(object);native_image_status(1,restore_failed,&status,sizeof(status));
    require(status.state==AAMOD_NATIVE_FAILED&&status.restore_count==0&&bindings.count(7),"failed original upload is not reported as restored");
    require(native_image_forget(1,restore_failed)==AAMOD_ERR_NATIVE_PENDING,"failed restoration retains ownership until resolved");
    native_image_restore(1,restore_failed);aamod::upload(object);native_image_status(1,restore_failed,&status,sizeof(status));
    require(status.state==AAMOD_NATIVE_RESTORED&&status.error==0&&status.restore_count==1,"explicit restoration retry can succeed");
    for(int i=0;i<20;++i){uint64_t h=0;native_image_replace(1,7,"good",&h);native_image_restore(1,h);}
    require(native_image_replace(1,7,"good",&other)==AAMOD_ERR_RESOURCE_LIMIT,"bounded retained status records");
    require(native_image_forget(1,handle)==0&&native_image_status(1,handle,&status,sizeof(status))==AAMOD_ERR_RESOURCE_HANDLE,"forget completed status");
    require(native_image_replace(1,7,"good",&other)==0,"reuse freed status slot");native_image_restore(1,other);
    native_images_revoke(1);require(replacements.empty()&&bindings.empty()&&allocations==frees,"owner cleanup and native allocation balance");
    available=false;puts("native image contracts passed: cache, alpha/orientation, owner/conflict, cancel/restore, failures and limits");
}

