#undef NDEBUG
#include <cassert>
#include <cstdio>
#include "aamod/menu.hpp"
int main() {
    using namespace aamod_sdk;
    std::vector<MenuItem> values={{144,L"迅捷光环",L"Swift [icon144]"},{244,L"火焰",L"Flame [icon144]"},{3,L"寒冰",L"Frost"}};
    assert(menu_filter(values,L"144")==std::vector<size_t>{0});
    assert(menu_filter(values,L"迅捷")==std::vector<size_t>{0});
    assert(menu_filter(values,L"SWIFT")==std::vector<size_t>{0});
    assert(menu_filter(values,L"不存在").empty());
    assert(menu_filter(values,L"").size()==3);
    assert(menu_utf8("寒冰")==L"寒冰");
    bool failed=false;try{menu_utf8("\xff");}catch(const std::runtime_error&){failed=true;}assert(failed);
    Menu menu;menu.close();assert(menu.run(nullptr,L"closed before startup")==0);
    puts("menu model: exact numeric IDs, Unicode and case-insensitive aliases, empty results, invalid UTF-8 and close-before-open passed");
}
