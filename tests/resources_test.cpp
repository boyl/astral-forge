#include "../src/core/resources.cpp"
#include <cstdio>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"failed %d: %s\n",__LINE__,#x);return 1;}} while(0)
static std::string utf8(const wchar_t* path) {
    int size=WideCharToMultiByte(CP_UTF8,0,path,-1,nullptr,0,nullptr,nullptr);
    std::string result(size,'\0');WideCharToMultiByte(CP_UTF8,0,path,-1,result.data(),size,nullptr,nullptr);result.resize(size-1);return result;
}
static volatile LONG failed;
static HANDLE copy_started;
static DWORD WINAPI copy_worker(void* token) {
    uint64_t owner=(uint64_t)token;
    for(unsigned i=0;i<200;++i){
        std::vector<uint8_t> pixels;uint32_t width=0,height=0;
        auto error=aamod::resources_image_copy(owner,"assets/rgba.png",pixels,width,height);
        if(error==AAMOD_OK){if(width!=2||height!=1||pixels.size()!=8||pixels[3]!=128)InterlockedExchange(&failed,1);}
        else if(error!=AAMOD_ERR_RESOURCE_HANDLE)InterlockedExchange(&failed,1);
        if(i==0)SetEvent(copy_started);
    }
    return 0;
}
static DWORD WINAPI worker(void* token) {
    uint64_t owner=(uint64_t)token;
    for(unsigned i=0;i<100;++i) {
        AAModImage image={};
        if(aamod::resources_image_load(owner,"assets/rgba.png",&image,sizeof(image)) ||
            image.width!=2 || image.pixels[3]!=128 || aamod::resources_image_release(owner,image.handle))
            InterlockedExchange(&failed,1);
    }
    return 0;
}
int wmain(int argc,wchar_t** argv) {
    using namespace aamod;CHECK(argc==2);
    uint64_t owner=resources_owner(utf8(argv[1]));CHECK(owner);
    uint64_t other=resources_owner(utf8(argv[1]));CHECK(other);
    AAModImage image={};
    CHECK(resources_image_load(owner,"assets/rgba.png",&image,sizeof(image))==0);
    CHECK(image.width==2 && image.height==1 && image.stride==8 && image.byte_count==8);
    CHECK(image.pixels[0]==0 && image.pixels[1]==180 && image.pixels[2]==255 && image.pixels[3]==128);
    CHECK(image.pixels[4]==255 && image.pixels[5]==120 && image.pixels[6]==0 && image.pixels[7]==255);
    CHECK(resources_image_release(other,image.handle)==AAMOD_ERR_RESOURCE_HANDLE);
    auto handle=image.handle;CHECK(resources_image_release(owner,handle)==0);
    CHECK(resources_image_release(owner,handle)==AAMOD_ERR_RESOURCE_HANDLE);
    std::vector<uint8_t> copied;uint32_t width=0,height=0;
    CHECK(resources_image_copy(owner,"assets/rgba.png",copied,width,height)==0);
    CHECK(width==2&&height==1&&copied.size()==8&&copied[3]==128&&resources_live_images()==0);
    CHECK(resources_image_load(owner,"assets/rgb.png",&image,sizeof(image))==0 && image.pixels[3]==255);
    CHECK(resources_image_release(owner,image.handle)==0);
    CHECK(resources_image_load(owner,"assets/gray.png",&image,sizeof(image))==0 && image.pixels[0]==77 && image.pixels[1]==77 && image.pixels[2]==77 && image.pixels[3]==255);
    CHECK(resources_image_release(owner,image.handle)==0);
    CHECK(resources_image_load(owner,"assets/palette.png",&image,sizeof(image))==0 && image.pixels[0]==5 && image.pixels[1]==15 && image.pixels[2]==25 && image.pixels[3]==0);
    CHECK(resources_image_release(owner,image.handle)==0);
    CHECK(resources_image_load(owner,"assets/中文.png",&image,sizeof(image))==0);CHECK(resources_image_release(owner,image.handle)==0);
    memset(&image,0xa5,sizeof(image));AAModImage before=image;
    for(auto path:{"../outside.png","assets/../../outside.png","C:/outside.png","\\\\server\\share\\outside.png","assets/rgba.png:secret","./assets/rgba.png","escape/outside.png"}) {
        CHECK(resources_image_load(owner,path,&image,sizeof(image))==AAMOD_ERR_RESOURCE_PATH);
        CHECK(memcmp(&image,&before,sizeof(image))==0);
    }
    CHECK(resources_image_load(owner,"assets/missing.png",&image,sizeof(image))==AAMOD_ERR_RESOURCE_IO);
    CHECK(resources_image_load(owner,"assets/corrupt.png",&image,sizeof(image))==AAMOD_ERR_RESOURCE_FORMAT);
    CHECK(resources_image_load(owner,"assets/not-png.bmp",&image,sizeof(image))==AAMOD_ERR_RESOURCE_FORMAT);
    CHECK(resources_image_load(owner,"assets/encoded-large.png",&image,sizeof(image))==AAMOD_ERR_RESOURCE_LIMIT);
    CHECK(resources_image_load(owner,"assets/axis-large.png",&image,sizeof(image))==AAMOD_ERR_RESOURCE_LIMIT);
    CHECK(resources_image_load(owner,"assets/pixels-large.png",&image,sizeof(image))==AAMOD_ERR_RESOURCE_LIMIT);
    CHECK(resources_image_load(owner,"assets/rgba.png",&image,sizeof(image)-1)==AAMOD_ERR_ARGUMENT);
    CHECK(memcmp(&image,&before,sizeof(image))==0);
    uint64_t handles[16];
    for(unsigned i=0;i<16;++i){CHECK(resources_image_load(owner,"assets/rgba.png",&image,sizeof(image))==0);handles[i]=image.handle;}
    CHECK(resources_image_load(owner,"assets/rgba.png",&image,sizeof(image))==AAMOD_ERR_RESOURCE_LIMIT);
    for(auto id:handles)CHECK(resources_image_release(owner,id)==0);
    HRESULT apartment=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);CHECK(SUCCEEDED(apartment));
    CHECK(resources_image_load(owner,"assets/rgba.png",&image,sizeof(image))==0);CHECK(resources_image_release(owner,image.handle)==0);
    HRESULT still_active=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);CHECK(still_active==S_FALSE);CoUninitialize();CoUninitialize();
    HANDLE threads[3];for(auto& thread:threads){thread=CreateThread(nullptr,0,worker,(void*)owner,0,nullptr);CHECK(thread);}
    CHECK(WaitForMultipleObjects(3,threads,TRUE,30000)==WAIT_OBJECT_0);for(auto thread:threads)CloseHandle(thread);CHECK(!failed);
    uint64_t copy_owner=resources_owner(utf8(argv[1]));CHECK(copy_owner);
    copy_started=CreateEventW(nullptr,TRUE,FALSE,nullptr);CHECK(copy_started);
    HANDLE copies[2];for(auto& thread:copies){thread=CreateThread(nullptr,0,copy_worker,(void*)copy_owner,0,nullptr);CHECK(thread);}
    CHECK(WaitForSingleObject(copy_started,10000)==WAIT_OBJECT_0);resources_revoke(copy_owner);
    CHECK(WaitForMultipleObjects(2,copies,TRUE,30000)==WAIT_OBJECT_0);for(auto thread:copies)CloseHandle(thread);CloseHandle(copy_started);CHECK(!failed&&resources_live_images()==0);
    CHECK(resources_image_load(owner,"assets/rgba.png",&image,sizeof(image))==0);handle=image.handle;
    resources_revoke(owner);CHECK(images.empty());
    CHECK(resources_image_release(owner,handle)==AAMOD_ERR_RESOURCE_HANDLE);
    CHECK(resources_image_load(owner,"assets/rgba.png",&image,sizeof(image))==AAMOD_ERR_RESOURCE_HANDLE);
    CHECK(copied[1]==180&&copied[3]==128); // copied payload survives owner revocation
    resources_revoke(other);CHECK(owners.empty());puts("PNG resource contracts passed");return 0;
}
