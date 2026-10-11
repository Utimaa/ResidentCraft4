#include "../native/native_window_hooks.hpp"
#include <cstdio>
#include <stdexcept>
#include <array>
#include <cstring>
#include <csetjmp>
using namespace re4craft;
namespace {
unsigned checks{},calls{},begins{},completes{};
bool wanted{},originalResult{},inside{};uint64_t nextToken{400},observed{};
bool yieldOriginal{};std::jmp_buf scheduler;
std::array<uint8_t,0x104> window{};
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
__declspec(noinline) bool __cdecl originalWindow(void* object){
    ++calls;check(object==window.data(),"cdecl original argument changed");
    check(inside==wanted,"native action did not run within the exact start/end scope");
    if(yieldOriginal)std::longjmp(scheduler,1);
    // This is an ABI fixture, not a replacement animation/physics implementation.
    return originalResult;
}
__declspec(noinline) bool caller(){volatile bool result=originalWindow(window.data());return result;}
__declspec(naked) bool originalFinish(){
    __asm { nop }
    __asm { nop }
    __asm { nop }
    __asm { nop }
    __asm { nop }
    __asm { nop }
    __asm { mov al,1 }
    __asm { ret }
}
// Match the verified retail EBP frame/argument at its success epilogue.
__declspec(naked) bool __cdecl resumeFinish(void* object){
    __asm { push ebp }
    __asm { mov ebp,esp }
    __asm { call originalFinish }
    __asm { pop ebp }
    __asm { ret }
}
uint64_t begin(void* context){++begins;check(context==&wanted,"window callback context changed");
    if(!wanted)return 0;inside=true;observed=++nextToken;return observed;}
void complete(void* context,uint64_t token,bool result){++completes;
    check(context==&wanted&&token==observed&&inside,"completion token/scope lost");
    check(result==originalResult,"AL original success result changed");inside=false;}
void kind(uint8_t k,uint32_t flags){uint32_t vtable=0x123400;window.fill(0);std::memcpy(window.data(),&vtable,4);std::memcpy(window.data()+4,&flags,4);window[0x100]=k;}
}
int main(){try{
    NativeWindowHooks hooks;kind(0x56,1);
    check(!hooks.installFixture(reinterpret_cast<uintptr_t>(originalWindow),nullptr,complete,&wanted),"missing begin installed");
    check(!hooks.installFixture(reinterpret_cast<uintptr_t>(originalWindow),begin,nullptr,&wanted),"missing completion installed");
    check(hooks.installFixture(reinterpret_cast<uintptr_t>(originalWindow),begin,complete,&wanted)&&hooks.installed(),"actual x86 MinHook window gateway failed");
    originalResult=true;wanted=false;
    check(caller()&&begins==1&&!completes,"native mode/F8-off changed original behavior");
    wanted=true;
    check(caller()&&begins==2&&completes==1&&!inside,"eligible window original success/cleanup failed");
    originalResult=false;
    check(!caller()&&begins==3&&completes==2&&!inside,"native failed action was forced successful or leaked ownership");
    wanted=false;kind(0x55,1);const auto saved=begins;
    check(!caller()&&begins==saved,"different native actor began a window handoff");
    kind(0x56,0);check(!caller()&&begins==saved,"inactive window gained authority");
    kind(0x56,0x201);check(!caller()&&begins==saved,"destroyed window gained authority");
    kind(0x56,1);
    for(unsigned n=0;n<2000;++n){wanted=(n&1)!=0;originalResult=(n&2)!=0;
        check(caller()==originalResult,"repeated cdecl/AL original return was corrupted");
        check(!inside,"repeated callback leaked original action scope");
    }
    hooks.removeFixture();wanted=false;originalResult=true;
    const auto oldBegins=begins;check(!hooks.installed()&&caller()&&begins==oldBegins,"removed hook changed restored native callback");
    check(calls==2007,"an original native coroutine was skipped or duplicated");
    check(hooks.installFixture(reinterpret_cast<uintptr_t>(originalWindow),begin,complete,&wanted,reinterpret_cast<uintptr_t>(originalFinish)),"exact completion observer failed to install");
    wanted=true;originalResult=true;yieldOriginal=true;const auto beforeYield=completes;
    if(std::setjmp(scheduler)==0){caller();check(false,"cooperative scheduler did not abandon the C++ wrapper");}
    check(inside&&completes==beforeYield,"yield falsely completed traversal");
    std::array<uint8_t,0x104> foreign{};
    check(resumeFinish(foreign.data())&&inside&&completes==beforeYield,"foreign coroutine completed the owned traversal");
    check(resumeFinish(window.data())&&!inside&&completes==beforeYield+1,"actual native completion did not release abandoned wrapper ownership");
    check(resumeFinish(window.data())&&completes==beforeYield+1,"completion was duplicated");
    yieldOriginal=false;check(caller()&&!inside&&completes==beforeYield+2,"ordinary return stopped working with completion observer");
    hooks.removeFixture();
    std::printf("Native window callback: %u checks passed; real x86 MinHook/cdecl/AL round trips. No game process touched.\n",checks);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"Window callback fixture failed after %u checks: %s\n",checks,error.what());return 1;}}
