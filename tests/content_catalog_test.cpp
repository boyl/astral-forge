#include "../src/core/content_catalog.cpp"
#include "../src/core/game_profile.cpp"
#undef NDEBUG
#include <cassert>
#include <cstdio>
int wmain(int argc,wchar_t** argv) {
    using namespace aamod;
    uint32_t count=91,total=92;AAModCatalogEntry out[2]={};out[0].id=999;
    assert(content_catalog_query(AAMOD_CONTENT_AURA,"zh-CN",0,out,2,sizeof(*out),&count,&total)==AAMOD_ERR_CONTENT_UNSUPPORTED&&out[0].id==999);
    rows={{AAMOD_CONTENT_AURA,7,1,"火焰","Fire"},{AAMOD_CONTENT_AURA,8,4,"冰霜","Frost"},{AAMOD_CONTENT_SPELL,30,0,"火球","Fireball"}};availability=0;
    assert(content_catalog_query(1,"zh-CN",0,nullptr,0,sizeof(*out),&count,&total)==0&&count==0&&total==2);
    assert(content_catalog_query(1,"en",1,out,2,sizeof(*out),&count,&total)==0&&count==1&&out[0].id==8&&!strcmp(out[0].name,"Frost"));
    assert(content_catalog_query(2,"zh-CN",0,out,2,sizeof(*out),&count,&total)==0&&count==1&&out[0].id==30&&!strcmp(out[0].name,"火球"));
    assert(content_catalog_query(1,"en",2,out,2,sizeof(*out),&count,&total)==0&&count==0);
    assert(content_catalog_query(1,"en",3,out,2,sizeof(*out),&count,&total)==AAMOD_ERR_ARGUMENT);
    assert(content_catalog_query(9,"en",0,out,2,sizeof(*out),&count,&total)==AAMOD_ERR_ARGUMENT);
    assert(content_catalog_query(1,"fr",0,out,2,sizeof(*out),&count,&total)==AAMOD_ERR_ARGUMENT);
    assert(content_catalog_query(1,"en",0,out,2,sizeof(*out)-1,&count,&total)==AAMOD_ERR_ARGUMENT);
    assert(content_catalog_contains(1,0)&&content_catalog_contains(1,7)&&!content_catalog_contains(2,7));
    std::string name(189,'a');name+="中文";char short_name[192];name_copy(short_name,name);assert(strlen(short_name)==189);
    assert(label(Json::parse("{\"x\":\"[font=main]\\u706b\\u7403[/font]\"}"),"x")=="火球");
    if(argc==2) {
        rows.clear();identity.identity_status=AAMOD_GAME_IDENTITY_MATCH;
        content_catalog_initialize(argv[1]);assert(content_catalog_available());
        assert(content_catalog_contains(1,144)&&content_catalog_contains(1,294)&&content_catalog_contains(2,30)&&!content_catalog_contains(2,86)&&!content_catalog_contains(2,160));
        for(auto kind:{1u,2u}) {
            assert(content_catalog_query(kind,"en",0,nullptr,0,sizeof(*out),&count,&total)==0&&total>0);
            printf("native catalog kind=%u count=%u\n",kind,total);
        }
    }
    puts("catalog: paging, language, domain bounds, common-spell policy and UTF-8 truncation passed");
}
