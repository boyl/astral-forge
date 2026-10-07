/* 可选的 Windows C++17 SDK 菜单组件。按源码编译进插件，不跨 DLL 传递 C++ 对象。
 * 调用 run() 的插件线程拥有窗口；卸载前 close() 并等待该线程退出。
 * 回调运行在菜单线程，可提交 SDK 请求；不得阻塞等待游戏输入。
 */
#ifndef AAMOD_MENU_HPP
#define AAMOD_MENU_HPP
#include <windows.h>
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <algorithm>
#include <stdexcept>
#include <cwctype>
namespace aamod_sdk {
struct MenuItem { uint32_t id; std::wstring name,alias; };
inline std::wstring menu_utf8(const char* text) {
    int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,nullptr,0);
    if(!length)throw std::runtime_error("menu: invalid UTF-8");
    std::wstring result(length,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,result.data(),length);
    result.pop_back();return result;
}
inline std::wstring menu_lower(std::wstring text) {
    std::transform(text.begin(),text.end(),text.begin(),[](wchar_t ch){return (wchar_t)towlower(ch);});return text;
}
inline std::vector<size_t> menu_filter(const std::vector<MenuItem>& items,const std::wstring& search) {
    std::vector<size_t> result;auto query=menu_lower(search);
    bool numeric=!query.empty()&&query.find_first_not_of(L"0123456789")==std::wstring::npos;
    for(size_t i=0;i<items.size();++i) {
        if(numeric?std::to_wstring(items[i].id)==query:menu_lower(items[i].name+L" "+items[i].alias).find(query)!=std::wstring::npos)result.push_back(i);
    }return result;
}
enum class MenuAction { Apply,Save,Load };
class Menu {
public:
    std::function<void(MenuAction,uint32_t,uint32_t,uint32_t)> action;
    std::function<void()> tick;
    std::function<void(const char*)> diagnostic;
    std::function<void(uint32_t)> category;
    std::vector<MenuItem> items;
    void set_items(std::vector<MenuItem> value) {items=std::move(value);filter();}
    void status(const std::wstring& value) {if(status_)SetWindowTextW(status_,value.c_str());}
    void enable_apply(bool enabled) {if(apply_)EnableWindow(apply_,enabled);}
    void close() {closing_.store(true);if(auto window=window_.load())PostMessageW(window,WM_CLOSE,0,0);}
    int run(HWND owner,const wchar_t* title) {
        if(closing_.load())return 0;
        HMODULE module=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)&procedure,&module);
        class_name_=L"AAMod.Menu."+std::to_wstring((uintptr_t)module)+L"."+std::to_wstring((uintptr_t)this);
        WNDCLASSEXW klass={sizeof(klass)};klass.lpfnWndProc=procedure;klass.hInstance=module;klass.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));klass.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);klass.lpszClassName=class_name_.c_str();
        if(!RegisterClassExW(&klass))return (int)GetLastError();
        HWND window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_CONTROLPARENT,class_name_.c_str(),title,WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,760,600,owner,nullptr,module,this);
        if(!window){auto error=GetLastError();UnregisterClassW(class_name_.c_str(),module);return error?(int)error:ERROR_GEN_FAILURE;}
        window_.store(window);ShowWindow(window,SW_SHOW);SetForegroundWindow(window);SetFocus(search_);if(closing_.load())PostMessageW(window,WM_CLOSE,0,0);
        MSG message={};int result;
        while((result=GetMessageW(&message,nullptr,0,0))>0) {
            if(message.message==WM_KEYDOWN&&message.hwnd==search_&&message.wParam=='A'&&(GetKeyState(VK_CONTROL)&0x8000)){SendMessageW(search_,EM_SETSEL,0,-1);continue;}
            if((message.message==WM_KEYDOWN||message.message==WM_SYSKEYDOWN)&&message.wParam==VK_ESCAPE){if(diagnostic)diagnostic("escape pressed");close_key_=true;continue;}
            if((message.message==WM_KEYUP||message.message==WM_SYSKEYUP)&&message.wParam==VK_ESCAPE&&close_key_){if(diagnostic)diagnostic("escape released");close_key_=false;PostMessageW(window,WM_CLOSE,0,0);continue;}
            if(message.message==WM_KEYDOWN&&message.wParam==VK_RETURN&&message.hwnd==list_){execute(MenuAction::Apply);continue;}
            if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
        }
        window_.store(nullptr);UnregisterClassW(class_name_.c_str(),module);if(owner&&IsWindow(owner))SetForegroundWindow(owner);
        return result==-1?(int)GetLastError():0;
    }
    static HWND game_window() {
        struct Search {DWORD pid;HWND window;LONG area;} search={GetCurrentProcessId(),nullptr,0};
        EnumWindows([](HWND window,LPARAM raw)->BOOL {auto& value=*(Search*)raw;DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid!=value.pid||!IsWindowVisible(window)||GetWindow(window,GW_OWNER))return TRUE;RECT rect;GetClientRect(window,&rect);LONG area=rect.right*rect.bottom;if(area>value.area){value.area=area;value.window=window;}return TRUE;},(LPARAM)&search);
        return search.window;
    }
private:
    std::atomic<HWND> window_{nullptr};std::atomic<bool> closing_{false};std::wstring class_name_;
    HWND search_=nullptr,list_=nullptr,kind_=nullptr,slot_=nullptr,status_=nullptr,apply_=nullptr,save_=nullptr,load_=nullptr;
    HFONT font_=nullptr;int line_=20;std::vector<size_t> visible_;
    bool close_key_=false;
    HWND control(HWND parent,const wchar_t* klass,const wchar_t* text,DWORD style,int id) {
        auto window=CreateWindowExW(0,klass,text,WS_CHILD|WS_VISIBLE|style,0,0,0,0,parent,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);
        if(!window)throw std::runtime_error("menu: control creation failed");
        SendMessageW(window,WM_SETFONT,(WPARAM)font_,TRUE);return window;
    }
    void filter() {
        if(!list_||!search_)return;int length=GetWindowTextLengthW(search_);std::wstring query(length+1,L'\0');GetWindowTextW(search_,query.data(),length+1);query.resize(length);
        uint32_t old=UINT32_MAX;auto selected=SendMessageW(list_,LB_GETCURSEL,0,0);if(selected>=0&&(size_t)selected<visible_.size())old=items[visible_[(size_t)selected]].id;
        visible_=menu_filter(items,query);SendMessageW(list_,LB_RESETCONTENT,0,0);int restore=0;
        HDC dc=GetDC(list_);auto old_font=SelectObject(dc,font_);LONG widest=0;
        for(size_t index=0;index<visible_.size();++index){auto& item=items[visible_[index]];auto text=std::to_wstring(item.id)+L"  "+item.name;SendMessageW(list_,LB_ADDSTRING,0,(LPARAM)text.c_str());SIZE extent={};GetTextExtentPoint32W(dc,text.data(),(int)text.size(),&extent);widest=(std::max)(widest,extent.cx);if(item.id==old)restore=(int)index;}
        SelectObject(dc,old_font);ReleaseDC(list_,dc);SendMessageW(list_,LB_SETHORIZONTALEXTENT,widest+16,0);
        if(!visible_.empty())SendMessageW(list_,LB_SETCURSEL,restore,0);
        status(visible_.empty()?L"没有匹配内容；请修改搜索。":(SendMessageW(kind_,CB_GETCURSEL,0,0)==1?L"替换技能会清除该槽关联符文。支持中文、英文和 ID 搜索；Enter 应用。":L"支持中文、英文和 ID 搜索。选择内容后点击应用或按 Enter。"));
    }
    void execute(MenuAction value) {
        uint32_t id=0;auto selected=SendMessageW(list_,LB_GETCURSEL,0,0);
        if(value==MenuAction::Apply){if(!IsWindowEnabled(apply_)||selected<0||(size_t)selected>=visible_.size())return;id=items[visible_[(size_t)selected]].id;}
        uint32_t kind=(uint32_t)SendMessageW(kind_,CB_GETCURSEL,0,0)+1,slot=(uint32_t)SendMessageW(slot_,CB_GETCURSEL,0,0);
        if(action)action(value,kind,slot,id);
    }
    void layout(HWND window) {
        RECT rect;GetClientRect(window,&rect);int gap=12,button=line_+14,top=gap,width=rect.right-2*gap;
        MoveWindow(kind_,gap,top,160,200,TRUE);MoveWindow(slot_,gap+172,top,100,200,TRUE);MoveWindow(search_,gap+284,top,width-284,button,TRUE);
        top+=button+gap;int height=(std::max)(2*line_,(int)rect.bottom-top-3*button-3*gap);
        MoveWindow(list_,gap,top,width,height,TRUE);top+=height+gap;
        MoveWindow(apply_,gap,top,120,button,TRUE);MoveWindow(save_,gap+132,top,160,button,TRUE);MoveWindow(load_,gap+304,top,160,button,TRUE);
        MoveWindow(status_,gap,top+button+gap,width,2*button,TRUE);
    }
    LRESULT message(HWND window,UINT message,WPARAM w,LPARAM l) {
        switch(message) {
        case WM_CREATE: {
            HDC dc=GetDC(window);int dpi=GetDeviceCaps(dc,LOGPIXELSY);font_=CreateFontW(-MulDiv(12,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
            if(!font_){ReleaseDC(window,dc);return -1;}auto old=SelectObject(dc,font_);TEXTMETRICW metric;GetTextMetricsW(dc,&metric);line_=metric.tmHeight+metric.tmExternalLeading;SelectObject(dc,old);ReleaseDC(window,dc);
            kind_=control(window,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,1);for(auto text:{L"光环",L"通用技能"})SendMessageW(kind_,CB_ADDSTRING,0,(LPARAM)text);SendMessageW(kind_,CB_SETCURSEL,0,0);
            slot_=control(window,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,2);for(unsigned slot=1;slot<=5;++slot){auto text=L"槽位 "+std::to_wstring(slot);SendMessageW(slot_,CB_ADDSTRING,0,(LPARAM)text.c_str());}SendMessageW(slot_,CB_SETCURSEL,0,0);
            search_=control(window,L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,3);SendMessageW(search_,EM_SETLIMITTEXT,128,0);SendMessageW(search_,0x1501,TRUE,(LPARAM)L"搜索中文 / English / ID");
            list_=control(window,L"LISTBOX",L"",WS_BORDER|WS_VSCROLL|WS_HSCROLL|WS_TABSTOP|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT,4);
            apply_=control(window,L"BUTTON",L"应用到当前槽",WS_TABSTOP,5);save_=control(window,L"BUTTON",L"保存当前构筑",WS_TABSTOP,6);load_=control(window,L"BUTTON",L"载入保存构筑",WS_TABSTOP,7);status_=control(window,L"STATIC",L"",0,8);
            filter();layout(window);SetTimer(window,1,100,nullptr);return 0;}
        case WM_GETMINMAXINFO: {auto limits=(MINMAXINFO*)l;limits->ptMinTrackSize={640,480};return 0;}
        case WM_SIZE:layout(window);return 0;
        case WM_TIMER:if(close_key_&&!(GetAsyncKeyState(VK_ESCAPE)&0x8000)){if(diagnostic)diagnostic("escape released at timer");close_key_=false;PostMessageW(window,WM_CLOSE,0,0);}if(tick)tick();return 0;
        case WM_COMMAND:
            if(LOWORD(w)==IDCANCEL&&!l){if(diagnostic)diagnostic("native cancel");close_key_=true;return 0;}
            if(LOWORD(w)==3&&HIWORD(w)==EN_CHANGE){filter();return 0;}
            if(LOWORD(w)==1&&HIWORD(w)==CBN_SELCHANGE){unsigned kind=(unsigned)SendMessageW(kind_,CB_GETCURSEL,0,0)+1;SendMessageW(slot_,CB_RESETCONTENT,0,0);for(unsigned slot=1;slot<=(kind==1?5u:4u);++slot){auto text=L"槽位 "+std::to_wstring(slot);SendMessageW(slot_,CB_ADDSTRING,0,(LPARAM)text.c_str());}SendMessageW(slot_,CB_SETCURSEL,0,0);if(category)category(kind);return 0;}
            if(LOWORD(w)==5||(LOWORD(w)==4&&HIWORD(w)==LBN_DBLCLK)){execute(MenuAction::Apply);return 0;}
            if(LOWORD(w)==6){execute(MenuAction::Save);return 0;}if(LOWORD(w)==7){execute(MenuAction::Load);return 0;}break;
        case WM_CLOSE:DestroyWindow(window);return 0;
        case WM_DESTROY:KillTimer(window,1);if(font_){DeleteObject(font_);font_=nullptr;}window_.store(nullptr);PostQuitMessage(0);return 0;
        }return DefWindowProcW(window,message,w,l);
    }
    static LRESULT CALLBACK procedure(HWND window,UINT msg,WPARAM w,LPARAM l) {
        auto self=(Menu*)GetWindowLongPtrW(window,GWLP_USERDATA);
        if(msg==WM_NCCREATE){self=(Menu*)((CREATESTRUCTW*)l)->lpCreateParams;SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)self);}
        if(!self)return DefWindowProcW(window,msg,w,l);
        try{return self->message(window,msg,w,l);}catch(const std::exception&){if(msg==WM_CREATE)return -1;self->status(L"菜单操作失败；关闭后重开，查看插件日志。");return 0;}
    }
};
}
#endif
