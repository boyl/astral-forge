#undef NDEBUG
#include <cassert>
#include <cstdio>
#include "../src/core/native_equipment.h"
static void* expected_frame;static double expected_slot;
__declspec(noinline) static double receive(void* frame,double player,double slot) {
    assert(frame==expected_frame);assert(player==1.0);assert(slot==expected_slot);return player+slot+0.25;
}
int main() {
    int frame=17;expected_frame=&frame;
    for(unsigned slot=0;slot<4;++slot) {expected_slot=slot+1.0;assert(aamod::native_refresh_spell(receive,&frame,slot)==slot+2.25);}
    for(unsigned bits=0;bits<16;++bits) {
        std::string mask;for(unsigned i=0;i<4;++i)mask+=((bits>>i)&1)?'1':'0';
        std::string replacement;assert(aamod::native_clear_gambits("116*140x*140x*172x*"+mask,replacement));assert(replacement=="0*0x*0x*0x*"+mask);
    }
    for(unsigned states=0;states<256;++states) {
        std::string mask;for(unsigned i=0;i<4;++i)mask+=char('0'+((states>>(i*2))&3));
        // Real native format: upgrade entries, identity and empty separators.
        std::string suffix=mask+"*36*36*i=27**36*";
        std::string replacement;assert(aamod::native_clear_gambits("114*157*157*114*"+suffix,replacement));
        assert(replacement=="0*0x*0x*0x*"+suffix);
    }
    std::string replacement="unchanged";assert(!aamod::native_clear_gambits("0*0x*0x*0x*1234",replacement));assert(replacement=="unchanged");assert(!aamod::native_clear_gambits("0*0x*0x*1111",replacement));
    assert(!aamod::native_clear_gambits("0*0x*0x*0x*11111*36",replacement));assert(replacement=="unchanged");
    puts("native spell refresh: RCX frame, XMM1 player, XMM2 all four slots and XMM0 double return passed");
}
