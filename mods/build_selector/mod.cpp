#include "aamod/aamod.h"
#include "aamod/menu.hpp"
#include <mutex>
#include <memory>
#include <cstdio>
using aamod_sdk::Menu;
static const AAModAPI* api;static HANDLE stopping,thread;
static std::mutex menu_lock;static std::shared_ptr<Menu> current_menu;
struct Preset {uint32_t size,version,auras[5],spells[4];};
class Controller {
    Menu& menu;std::vector<AAModEquipmentCommand> batch;size_t position=0;uint64_t request_id=0;
    uint64_t epoch=0;bool running=false;
    void failure(const std::wstring& text){batch.clear();position=0;request_id=0;running=false;menu.status(text);AAMOD_LOGW(api,"build_selector: operation stopped");}
    bool catalog_has(uint32_t kind,uint32_t id) {
        if(kind==1&&!id)return true;uint32_t total=0,count=0;
        if(api->content_catalog(kind,"en",0,nullptr,0,sizeof(AAModCatalogEntry),&count,&total))return false;
        std::vector<AAModCatalogEntry> entries(total);
        if(api->content_catalog(kind,"en",0,entries.data(),total,sizeof(AAModCatalogEntry),&count,&total))return false;
        for(const auto& entry:entries)if(entry.id==id)return true;return false;
    }
    bool active(AAModEquipment& equipment) {
        if(api->equipment(&equipment,sizeof(equipment))||!equipment.valid){menu.status(L"当前无法装配：请进入受支持的单人本轮。仍可浏览目录。");return false;}return true;
    }
    void submit_next() {
        if(position==batch.size()){running=false;request_id=0;menu.status(L"应用成功。修改仅作用于当前本轮；保存构筑可供下次载入。");AAMOD_LOGI(api,"build_selector: applied %zu slot operations",batch.size());return;}
        AAModEquipment equipment={};if(!active(equipment)){failure(L"执行中断：本轮装备不可用。已完成的槽位仍保留，请重新检查。");return;}
        if(equipment.scene_epoch!=epoch){failure(L"执行中断：场景已变化。请查看当前装备，再重新应用。");return;}
        auto& request=batch[position];request.scene_epoch=epoch;request.expected_id=request.kind==1?equipment.auras[request.slot]:equipment.spells[request.slot];
        auto error=api->equipment_submit(api->command_owner,&request,sizeof(request),&request_id);
        if(error){failure(L"提交失败，错误码 "+std::to_wstring(error)+L"。已完成的槽位仍保留。");return;}
        menu.status(L"正在应用 "+std::to_wstring(position+1)+L" / "+std::to_wstring(batch.size())+L"；等待游戏原生更新。");
    }
public:
    explicit Controller(Menu& value):menu(value) {
        menu.diagnostic=[](const char* message){AAMOD_LOGI(api,"build_selector: menu %s",message);};
        menu.category=[this](uint32_t kind){category(kind);};
        menu.action=[this](aamod_sdk::MenuAction action,uint32_t kind,uint32_t slot,uint32_t id){execute(action,kind,slot,id);};
        menu.tick=[this]{tick();};category(1);
    }
    void category(uint32_t kind) {
        uint32_t count=0,total=0;auto error=api->content_catalog(kind,"zh-CN",0,nullptr,0,sizeof(AAModCatalogEntry),&count,&total);
        if(error){menu.set_items({});menu.status(L"目录不可用，错误码 "+std::to_wstring(error));return;}
        std::vector<AAModCatalogEntry> chinese(total),english(total);
        error=api->content_catalog(kind,"zh-CN",0,chinese.data(),total,sizeof(AAModCatalogEntry),&count,&total);
        if(!error)error=api->content_catalog(kind,"en",0,english.data(),total,sizeof(AAModCatalogEntry),&count,&total);
        if(error){menu.set_items({});menu.status(L"目录读取失败，错误码 "+std::to_wstring(error));return;}
        std::vector<aamod_sdk::MenuItem> items;if(kind==1)items.push_back({0,L"清空此光环槽",L"clear remove empty"});
        for(size_t index=0;index<chinese.size();++index)items.push_back({chinese[index].id,aamod_sdk::menu_utf8(chinese[index].name),aamod_sdk::menu_utf8(english[index].name)});
        menu.set_items(std::move(items));
    }
    void execute(aamod_sdk::MenuAction action,uint32_t kind,uint32_t slot,uint32_t id) {
        if(running){menu.status(L"正在处理上一操作；完成后可继续。关闭窗口不会重放操作。");return;}
        AAModEquipment equipment={};if(!active(equipment))return;
        if(action==aamod_sdk::MenuAction::Save) {
            Preset preset={sizeof(preset),1};memcpy(preset.auras,equipment.auras,sizeof(preset.auras));memcpy(preset.spells,equipment.spells,sizeof(preset.spells));
            auto error=api->data_write(api->data_owner,"build-preset",&preset,sizeof(preset));
            menu.status(error?L"保存失败，错误码 "+std::to_wstring(error):L"构筑已保存到本插件数据。重启游戏后可载入。关联符文不包含在此示例预设中。");AAMOD_LOGI(api,"build_selector: save result=%u",error);return;
        }
        batch.clear();position=0;epoch=equipment.scene_epoch;
        if(action==aamod_sdk::MenuAction::Load) {
            Preset preset={};uint32_t size=0;auto error=api->data_read(api->data_owner,"build-preset",&preset,sizeof(preset),&size);
            if(error){menu.status(L"未能载入预设，错误码 "+std::to_wstring(error)+L"。可先保存当前构筑。");return;}
            if(size!=sizeof(preset)||preset.size!=sizeof(preset)||preset.version!=1){menu.status(L"预设格式不兼容；当前装备未修改。");return;}
            for(unsigned i=0;i<9;++i) {uint32_t content_kind=i<5?1u:2u,content_id=i<5?preset.auras[i]:preset.spells[i-5];if(!catalog_has(content_kind,content_id)){menu.status(L"预设包含当前目录不支持的 ID；当前装备未修改。");return;}}
            for(unsigned i=0;i<9;++i){uint32_t content_kind=i<5?1u:2u,content_id=i<5?preset.auras[i]:preset.spells[i-5];uint32_t old=i<5?equipment.auras[i]:equipment.spells[i-5];if(content_id!=old)batch.push_back({sizeof(AAModEquipmentCommand),content_kind,epoch,i<5?i:i-5,content_id,old,0});}
        } else {
            if(id==(kind==1?equipment.auras[slot]:equipment.spells[slot])){menu.status(L"此槽已装配所选内容；无需修改。");return;}
            if(kind==2)menu.status(L"替换技能将清除此槽关联符文。");
            batch.push_back({sizeof(AAModEquipmentCommand),kind,epoch,slot,id,kind==1?equipment.auras[slot]:equipment.spells[slot],0});
        }
        running=true;submit_next();
    }
    void tick() {
        AAModEquipment equipment={};api->equipment(&equipment,sizeof(equipment));menu.enable_apply(equipment.valid&&!running);
        if(!running||!request_id)return;AAModEquipmentResult result={};auto error=api->equipment_result(api->command_owner,request_id,&result,sizeof(result));
        if(error){failure(L"结果读取失败，错误码 "+std::to_wstring(error));return;}
        if(result.status==AAMOD_EQUIPMENT_PENDING)return;
        if(result.status!=AAMOD_EQUIPMENT_APPLIED){failure(L"应用失败，状态 "+std::to_wstring(result.status)+L"，错误 "+std::to_wstring(result.error)+L"。已完成的槽位仍保留。");return;}
        ++position;submit_next();
    }
};
static DWORD WINAPI run(void*) {
    bool down=false;
    while(WaitForSingleObject(stopping,50)==WAIT_TIMEOUT) {
        bool pressed=(GetAsyncKeyState(VK_F10)&0x8000)!=0;auto owner=Menu::game_window();
        if(pressed&&!down)AAMOD_LOGI(api,"build_selector: open key owner=%p foreground=%p",owner,GetForegroundWindow());
        if(pressed&&!down&&owner&&GetForegroundWindow()==owner) {
            auto menu=std::make_shared<Menu>();Controller controller(*menu);AAMOD_LOGI(api,"build_selector: menu model ready");
            {std::lock_guard<std::mutex> lock(menu_lock);current_menu=menu;}
            if(WaitForSingleObject(stopping,0)==WAIT_OBJECT_0)menu->close();
            auto error=menu->run(owner,L"Astral Forge — 构筑选择器（F10 打开 / Esc 关闭）");
            AAMOD_LOGI(api,"build_selector: menu closed result=%d",error);
            {std::lock_guard<std::mutex> lock(menu_lock);current_menu.reset();}
            if(error)AAMOD_LOGE(api,"build_selector: menu window error=%d",error);
            // Require the opening key to be released before another window.
            down=true;
        } else down=pressed;
    }return 0;
}
AAMOD_EXPORT uint32_t AAMOD_Init(const AAModAPI* value,uint32_t size) {
    if(!value||size<320||value->api_version!=1||!value->data_owner)return AAMOD_ERR_ABI;
    api=value;stopping=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!stopping)return AAMOD_ERR_GENERIC;
    thread=CreateThread(nullptr,0,run,nullptr,0,nullptr);if(!thread){CloseHandle(stopping);stopping=nullptr;return AAMOD_ERR_GENERIC;}
    AAMOD_LOGI(api,"build_selector: F10 menu ready; public SDK only");return 0;
}
AAMOD_EXPORT void AAMOD_Shutdown() {
    SetEvent(stopping);{std::lock_guard<std::mutex> lock(menu_lock);if(current_menu)current_menu->close();}
    WaitForSingleObject(thread,INFINITE);CloseHandle(thread);CloseHandle(stopping);thread=stopping=nullptr;
}
