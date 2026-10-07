#include "plugin_data.h"
#include <windows.h>
#include <vector>
#include <algorithm>
#include <cstring>
namespace aamod {
namespace {
SRWLOCK lock=SRWLOCK_INIT;
struct Guard { Guard(){AcquireSRWLockExclusive(&lock);} ~Guard(){ReleaseSRWLockExclusive(&lock);} };
struct File { HANDLE h=INVALID_HANDLE_VALUE; ~File(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);} };
struct Owner {uint64_t token;std::wstring root;std::string id;};
std::vector<Owner> owners;
uint64_t next_token;
constexpr uint32_t limit=1024*1024;
std::wstring resolved(HANDLE h) {
    DWORD n=GetFinalPathNameByHandleW(h,nullptr,0,FILE_NAME_NORMALIZED);
    if(!n)return {};
    std::wstring s(n,L'\0');DWORD got=GetFinalPathNameByHandleW(h,s.data(),n,FILE_NAME_NORMALIZED);
    if(!got || got>=n)return {};s.resize(got);return s;
}
bool valid_key(const char* key) {
    if(!key)return false;size_t n=strnlen(key,65);if(!n || n>64)return false;
    for(size_t i=0;i<n;++i) {unsigned char c=key[i];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'))return false;}
    return true;
}
Owner* find(uint64_t token){for(auto& o:owners)if(o.token==token)return &o;return nullptr;}
std::wstring path(const Owner& o,const char* key){std::wstring s=o.root;for(;*key;++key)s+=wchar_t(*key);return s+L".dat";}
bool inside(const Owner& o,HANDLE h) {
    auto p=resolved(h);return p.size()>o.root.size() && _wcsnicmp(p.c_str(),o.root.c_str(),o.root.size())==0;
}
uint32_t check_quota(const Owner& o,const std::wstring& target,uint32_t size) {
    WIN32_FIND_DATAW found={};HANDLE scan=FindFirstFileW((o.root+L"*.dat").c_str(),&found);
    if(scan==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_FILE_NOT_FOUND?AAMOD_OK:AAMOD_ERR_DATA_IO;
    uint64_t total=size;unsigned count=1;
    do {
        if(found.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)continue;
        auto entry=o.root+found.cFileName;
        if(_wcsicmp(entry.c_str(),target.c_str())==0)continue;
        total+=(uint64_t(found.nFileSizeHigh)<<32)|found.nFileSizeLow;++count;
    } while(FindNextFileW(scan,&found));
    DWORD error=GetLastError();FindClose(scan);
    if(error!=ERROR_NO_MORE_FILES)return AAMOD_ERR_DATA_IO;
    return total>16*limit || count>64?AAMOD_ERR_DATA_LIMIT:AAMOD_OK;
}
}
uint64_t plugin_data_owner(const std::wstring& data,const std::string& id) {
    if(id.empty() || id.size()>128)return 0;
    Guard guard;for(const auto& o:owners)if(o.id==id)return 0;
    File data_handle;data_handle.h=CreateFileW(data.c_str(),0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    if(data_handle.h==INVALID_HANDLE_VALUE)return 0;
    auto data_root=resolved(data_handle.h);if(data_root.empty())return 0;data_root+=L'\\';
    std::wstring parent=data+L"\\plugin-data";
    if(!CreateDirectoryW(parent.c_str(),nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS)return 0;
    const wchar_t* hex=L"0123456789abcdef";std::wstring directory=parent+L"\\";
    for(unsigned char c:id){directory+=hex[c>>4];directory+=hex[c&15];}
    if(!CreateDirectoryW(directory.c_str(),nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS)return 0;
    File f;f.h=CreateFileW(directory.c_str(),0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    if(f.h==INVALID_HANDLE_VALUE)return 0;
    auto root=resolved(f.h);if(root.size()<=data_root.size() || _wcsnicmp(root.c_str(),data_root.c_str(),data_root.size())!=0)return 0;root+=L'\\';
    uint64_t token=++next_token;owners.push_back({token,std::move(root),id});return token;
}
void plugin_data_revoke(uint64_t token) {
    Guard guard;owners.erase(std::remove_if(owners.begin(),owners.end(),[token](const Owner& o){return o.token==token;}),owners.end());
}
uint32_t plugin_data_read(uint64_t token,const char* key,void* bytes,uint32_t capacity,uint32_t* size) {
    if(!size || (!bytes&&capacity))return AAMOD_ERR_ARGUMENT;
    if(!valid_key(key))return AAMOD_ERR_DATA_KEY;
    Guard guard;auto o=find(token);if(!o)return AAMOD_ERR_DATA_OWNER;
    File f;f.h=CreateFileW(path(*o,key).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f.h==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_FILE_NOT_FOUND?AAMOD_ERR_DATA_MISSING:AAMOD_ERR_DATA_IO;
    if(!inside(*o,f.h))return AAMOD_ERR_DATA_KEY;
    LARGE_INTEGER length;if(!GetFileSizeEx(f.h,&length))return AAMOD_ERR_DATA_IO;
    if(length.QuadPart<0 || length.QuadPart>limit)return AAMOD_ERR_DATA_LIMIT;
    *size=(uint32_t)length.QuadPart;
    if(!bytes)return AAMOD_OK;
    if(capacity<*size)return AAMOD_ERR_DATA_BUFFER;
    std::vector<unsigned char> copy(*size);DWORD got=0;
    if(*size && (!ReadFile(f.h,copy.data(),*size,&got,nullptr)||got!=*size))return AAMOD_ERR_DATA_IO;
    if(*size)memcpy(bytes,copy.data(),*size);return AAMOD_OK;
}
uint32_t plugin_data_write(uint64_t token,const char* key,const void* bytes,uint32_t size) {
    if(!bytes&&size)return AAMOD_ERR_ARGUMENT;
    if(size>limit)return AAMOD_ERR_DATA_LIMIT;
    if(!valid_key(key))return AAMOD_ERR_DATA_KEY;
    Guard guard;auto o=find(token);if(!o)return AAMOD_ERR_DATA_OWNER;
    auto target=path(*o,key),temporary=target+L".pending";
    uint32_t quota=check_quota(*o,target,size);if(quota)return quota;
    // Recover an interrupted previous write without following a link outside
    // the namespace. Its uncommitted bytes never replace the committed value.
    File stale;stale.h=CreateFileW(temporary.c_str(),DELETE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(stale.h!=INVALID_HANDLE_VALUE) {
        if(!inside(*o,stale.h))return AAMOD_ERR_DATA_KEY;
        FILE_DISPOSITION_INFO info={TRUE};if(!SetFileInformationByHandle(stale.h,FileDispositionInfo,&info,sizeof(info)))return AAMOD_ERR_DATA_IO;
        CloseHandle(stale.h);stale.h=INVALID_HANDLE_VALUE;
    } else if(GetLastError()!=ERROR_FILE_NOT_FOUND)return AAMOD_ERR_DATA_IO;
    File f;f.h=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f.h==INVALID_HANDLE_VALUE)return AAMOD_ERR_DATA_IO;
    bool safe=inside(*o,f.h);DWORD written=0;
    bool ok=safe && (!size || (WriteFile(f.h,bytes,size,&written,nullptr)&&written==size)) && FlushFileBuffers(f.h);
    CloseHandle(f.h);f.h=INVALID_HANDLE_VALUE;
    if(ok)ok=!!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    DeleteFileW(temporary.c_str());return ok?AAMOD_OK:(safe?AAMOD_ERR_DATA_IO:AAMOD_ERR_DATA_KEY);
}
uint32_t plugin_data_delete(uint64_t token,const char* key) {
    if(!valid_key(key))return AAMOD_ERR_DATA_KEY;
    Guard guard;auto o=find(token);if(!o)return AAMOD_ERR_DATA_OWNER;
    File f;f.h=CreateFileW(path(*o,key).c_str(),DELETE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f.h==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_FILE_NOT_FOUND?AAMOD_OK:AAMOD_ERR_DATA_IO;
    if(!inside(*o,f.h))return AAMOD_ERR_DATA_KEY;
    FILE_DISPOSITION_INFO info={TRUE};return SetFileInformationByHandle(f.h,FileDispositionInfo,&info,sizeof(info))?AAMOD_OK:AAMOD_ERR_DATA_IO;
}
}
