#include "../src/core/plugin_data.cpp"
#undef NDEBUG
#include <cassert>
#include <cstdio>
int wmain(int argc,wchar_t** argv) {
    assert(argc==2);std::wstring root=argv[1];CreateDirectoryW(root.c_str(),nullptr);
    auto a=aamod::plugin_data_owner(root,"alpha"),b=aamod::plugin_data_owner(root,"beta");assert(a&&b&&a!=b);
    assert(!aamod::plugin_data_owner(root,"alpha"));
    const char value[]={0,1,2,'x'};uint32_t size=99;char out[8]={42};
    assert(aamod::plugin_data_read(a,"build",out,sizeof(out),&size)==AAMOD_ERR_DATA_MISSING);
    assert(aamod::plugin_data_write(a,"build",value,sizeof(value))==0);
    assert(aamod::plugin_data_read(a,"build",nullptr,0,&size)==0&&size==4);
    assert(aamod::plugin_data_read(a,"build",out,3,&size)==AAMOD_ERR_DATA_BUFFER&&out[0]==42);
    assert(aamod::plugin_data_read(a,"build",out,sizeof(out),&size)==0&&!memcmp(value,out,4));
    assert(aamod::plugin_data_read(b,"build",out,sizeof(out),&size)==AAMOD_ERR_DATA_MISSING);
    for(const char* key:{"../build","a:b","a/b","a\\b","","你好"})assert(aamod::plugin_data_write(a,key,value,4)==AAMOD_ERR_DATA_KEY);
    assert(aamod::plugin_data_write(a,"build",value,1024*1024+1)==AAMOD_ERR_DATA_LIMIT);
    aamod::plugin_data_revoke(a);assert(aamod::plugin_data_write(a,"build",value,4)==AAMOD_ERR_DATA_OWNER);
    a=aamod::plugin_data_owner(root,"alpha");assert(a);
    assert(aamod::plugin_data_read(a,"build",out,sizeof(out),&size)==0&&!memcmp(value,out,4));
    auto pending=root+L"\\plugin-data\\616c706861\\build.dat.pending";
    HANDLE f=CreateFileW(pending.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);assert(f!=INVALID_HANDLE_VALUE);CloseHandle(f);
    assert(aamod::plugin_data_write(a,"build","new",3)==0);
    assert(aamod::plugin_data_read(a,"build",out,sizeof(out),&size)==0&&size==3&&!memcmp(out,"new",3));
    assert(aamod::plugin_data_write(a,"empty",nullptr,0)==0);
    assert(aamod::plugin_data_read(a,"empty",nullptr,0,&size)==0&&size==0);
    assert(aamod::plugin_data_delete(a,"build")==0&&aamod::plugin_data_delete(a,"build")==0);
    assert(aamod::plugin_data_delete(a,"empty")==0);aamod::plugin_data_revoke(a);aamod::plugin_data_revoke(b);
    auto q=aamod::plugin_data_owner(root,"quota");assert(q);
    for(int i=0;i<64;++i){auto key=std::string("k")+std::to_string(i);assert(aamod::plugin_data_write(q,key.c_str(),nullptr,0)==0);}
    assert(aamod::plugin_data_write(q,"overflow",nullptr,0)==AAMOD_ERR_DATA_LIMIT);
    assert(aamod::plugin_data_delete(q,"k0")==0&&aamod::plugin_data_write(q,"overflow",nullptr,0)==0);
    aamod::plugin_data_revoke(q);
    auto m=aamod::plugin_data_owner(root,"bytes");assert(m);std::vector<char> block(1024*1024,'b');
    for(int i=0;i<16;++i){auto key=std::string("k")+std::to_string(i);assert(aamod::plugin_data_write(m,key.c_str(),block.data(),(uint32_t)block.size())==0);}
    assert(aamod::plugin_data_write(m,"overflow","x",1)==AAMOD_ERR_DATA_LIMIT);
    assert(aamod::plugin_data_write(m,"k0","x",1)==0);
    assert(aamod::plugin_data_write(m,"overflow","x",1)==0);aamod::plugin_data_revoke(m);
    puts("plugin data: isolation, restart, binary/empty, bounds, atomic replacement and interrupted-write recovery passed");
}
