#include "resources.h"
#include "log.h"
#include <windows.h>
#include <wincodec.h>
#include <vector>
#include <algorithm>
#include <cstring>
#include <new>
namespace aamod {
namespace {
SRWLOCK resource_lock = SRWLOCK_INIT;
struct Owner { uint64_t id; std::wstring root; };
struct Image { uint64_t owner,id; uint32_t width,height; std::vector<uint8_t> pixels; };
std::vector<Owner> owners;
std::vector<Image> images;
uint64_t next_owner,next_image;
constexpr size_t MiB=1024*1024;
struct Guard {
    Guard(){AcquireSRWLockExclusive(&resource_lock);}
    ~Guard(){ReleaseSRWLockExclusive(&resource_lock);}
};
struct File {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~File(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
template<class T> struct Com {
    T* value=nullptr;
    ~Com(){if(value)value->Release();}
};
struct Apartment {
    HRESULT result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    ~Apartment(){if(SUCCEEDED(result))CoUninitialize();}
};
std::wstring wide(const char* text) {
    int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,nullptr,0);
    if(!size)return {};
    std::wstring output(size,L'\0');
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,output.data(),size))return {};
    output.resize(size-1);return output;
}
std::wstring final_path(HANDLE file) {
    DWORD size=GetFinalPathNameByHandleW(file,nullptr,0,FILE_NAME_NORMALIZED);
    if(!size)return {};
    std::wstring output(size,L'\0');
    DWORD actual=GetFinalPathNameByHandleW(file,output.data(),size,FILE_NAME_NORMALIZED);
    if(!actual || actual>=size)return {};
    output.resize(actual);return output;
}
uint32_t decode(std::vector<uint8_t>& bytes,Image* image) {
    Apartment apartment;
    if(FAILED(apartment.result) && apartment.result!=RPC_E_CHANGED_MODE)return AAMOD_ERR_RESOURCE_COM;
    Com<IWICImagingFactory> factory; Com<IWICStream> stream; Com<IWICBitmapDecoder> decoder;
    Com<IWICBitmapFrameDecode> frame; Com<IWICFormatConverter> converter;
    HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory.value));
    if(FAILED(hr))return AAMOD_ERR_RESOURCE_COM;
    hr=factory.value->CreateStream(&stream.value);
    if(SUCCEEDED(hr))hr=stream.value->InitializeFromMemory(bytes.data(),(DWORD)bytes.size());
    if(SUCCEEDED(hr))hr=factory.value->CreateDecoderFromStream(stream.value,nullptr,WICDecodeMetadataCacheOnLoad,&decoder.value);
    GUID format={};
    if(SUCCEEDED(hr))hr=decoder.value->GetContainerFormat(&format);
    if(FAILED(hr) || !IsEqualGUID(format,GUID_ContainerFormatPng))return AAMOD_ERR_RESOURCE_FORMAT;
    hr=decoder.value->GetFrame(0,&frame.value);
    if(SUCCEEDED(hr))hr=frame.value->GetSize(&image->width,&image->height);
    if(FAILED(hr))return AAMOD_ERR_RESOURCE_FORMAT;
    uint64_t count=uint64_t(image->width)*image->height*4;
    if(!image->width || !image->height || image->width>8192 || image->height>8192 || count>64*MiB)return AAMOD_ERR_RESOURCE_LIMIT;
    hr=factory.value->CreateFormatConverter(&converter.value);
    if(SUCCEEDED(hr))hr=converter.value->Initialize(frame.value,GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom);
    if(FAILED(hr))return AAMOD_ERR_RESOURCE_FORMAT;
    image->pixels.resize((size_t)count);
    hr=converter.value->CopyPixels(nullptr,image->width*4,(UINT)count,image->pixels.data());
    if(FAILED(hr))return AAMOD_ERR_RESOURCE_FORMAT;
    return AAMOD_OK;
}
}
uint64_t resources_owner(const std::string& directory) {
    auto path=wide(directory.c_str()); if(path.empty())return 0;
    File file;file.value=CreateFileW(path.c_str(),0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    if(file.value==INVALID_HANDLE_VALUE)return 0;
    auto root=final_path(file.value);if(root.empty())return 0;
    if(root.back()!=L'\\')root+=L'\\';
    Guard guard;uint64_t id=++next_owner;owners.push_back({id,std::move(root)});return id;
}
void resources_revoke(uint64_t owner) {
    Guard guard;
    images.erase(std::remove_if(images.begin(),images.end(),[owner](const Image& image){return image.owner==owner;}),images.end());
    owners.erase(std::remove_if(owners.begin(),owners.end(),[owner](const Owner& value){return value.id==owner;}),owners.end());
}
size_t resources_live_images() { Guard guard; return images.size(); }
static uint32_t image_load(uint64_t owner,const char* relative,AAModImage* output,uint32_t size) {
    if(!relative || !output || size<sizeof(*output) || !*relative || strnlen(relative,32768)==32768)return AAMOD_ERR_ARGUMENT;
    auto path=wide(relative); if(path.empty())return AAMOD_ERR_RESOURCE_PATH;
    std::replace(path.begin(),path.end(),L'/',L'\\');
    if(path.front()==L'\\' || path.find(L':')!=std::wstring::npos)return AAMOD_ERR_RESOURCE_PATH;
    for(size_t start=0;start<path.size();) {
        auto end=path.find(L'\\',start);auto part=path.substr(start,end==std::wstring::npos?end:end-start);
        if(part.empty() || part==L"." || part==L"..")return AAMOD_ERR_RESOURCE_PATH;
        if(end==std::wstring::npos)break;start=end+1;
    }
    Guard guard;auto found=std::find_if(owners.begin(),owners.end(),[owner](const Owner& value){return value.id==owner;});
    if(!owner || found==owners.end())return AAMOD_ERR_RESOURCE_HANDLE;
    File file;auto requested=found->root+path;
    file.value=CreateFileW(requested.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file.value==INVALID_HANDLE_VALUE){AAMOD_WARN("resources: open failed error=%lu",GetLastError());return AAMOD_ERR_RESOURCE_IO;}
    auto actual=final_path(file.value);
    if(actual.size()<=found->root.size() || CompareStringOrdinal(actual.c_str(),(int)found->root.size(),found->root.c_str(),(int)found->root.size(),TRUE)!=CSTR_EQUAL)
        return AAMOD_ERR_RESOURCE_PATH;
    LARGE_INTEGER length;
    if(!GetFileSizeEx(file.value,&length))return AAMOD_ERR_RESOURCE_IO;
    if(length.QuadPart<=0 || length.QuadPart>32*MiB)return AAMOD_ERR_RESOURCE_LIMIT;
    std::vector<uint8_t> bytes((size_t)length.QuadPart);DWORD read=0;
    if(!ReadFile(file.value,bytes.data(),(DWORD)bytes.size(),&read,nullptr) || read!=bytes.size())return AAMOD_ERR_RESOURCE_IO;
    Image image={owner,0,0,0,{}};auto error=decode(bytes,&image);
    if(error)return error;
    size_t total=0,own=0;unsigned owned_count=0;
    for(const auto& value:images){total+=value.pixels.size();if(value.owner==owner){own+=value.pixels.size();++owned_count;}}
    if(owned_count>=16 || own+image.pixels.size()>64*MiB || total+image.pixels.size()>256*MiB)return AAMOD_ERR_RESOURCE_LIMIT;
    image.id=++next_image;images.push_back(std::move(image));const auto& value=images.back();
    AAModImage result={sizeof(result),value.width,value.height,value.width*4,(uint32_t)value.pixels.size(),0,value.id,value.pixels.data()};
    *output=result;return AAMOD_OK;
}
uint32_t resources_image_load(uint64_t owner,const char* relative,AAModImage* output,uint32_t size) {
    try { return image_load(owner,relative,output,size); }
    catch (const std::bad_alloc&) { return AAMOD_ERR_RESOURCE_MEMORY; }
}
uint32_t resources_image_release(uint64_t owner,uint64_t id) {
    if(!owner || !id)return AAMOD_ERR_ARGUMENT;
    Guard guard;auto found=std::find_if(images.begin(),images.end(),[owner,id](const Image& image){return image.owner==owner && image.id==id;});
    if(found==images.end())return AAMOD_ERR_RESOURCE_HANDLE;
    images.erase(found);return AAMOD_OK;
}
uint32_t resources_image_copy(uint64_t owner,const char* path,std::vector<uint8_t>& pixels,uint32_t& width,uint32_t& height) {
    AAModImage result={};auto error=resources_image_load(owner,path,&result,sizeof(result));if(error)return error;
    Guard guard;
    auto found=std::find_if(images.begin(),images.end(),[owner,&result](const Image& value){return value.owner==owner&&value.id==result.handle;});
    if(found==images.end())return AAMOD_ERR_RESOURCE_HANDLE;
    try {pixels=found->pixels;width=found->width;height=found->height;}
    catch(const std::bad_alloc&){images.erase(found);return AAMOD_ERR_RESOURCE_MEMORY;}
    images.erase(found);return AAMOD_OK;
}
}
