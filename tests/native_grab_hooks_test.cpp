#include "../native/native_grab_hooks.hpp"
#include <cstdio>
#include <intrin.h>
#include <stdexcept>
using namespace re4craft;
namespace {
unsigned checks{},originalCalls{},eligibilityCalls{};
volatile uintptr_t observedReturn{};
bool protectedProxy{};
void check(bool condition,const char* text){++checks;if(!condition)throw std::runtime_error(text);}
__declspec(noinline) int __cdecl originalCounter(){
    observedReturn=reinterpret_cast<uintptr_t>(_ReturnAddress());++originalCalls;return 37;
}
// The volatile result requires a real return to the caller (no tail call),
// independently proving the actual ABI gateway preserves its stack/results.
#define CALLER(name,extra) __declspec(noinline) int name(){volatile int result=originalCounter();return result+extra;}
CALLER(neck,1) CALLER(neckLater,2) CALLER(neckRandom,3)
CALLER(bomb,4) CALLER(back,5) CALLER(backRandom,6) CALLER(unrelated,7)
bool eligibility(void* context){++eligibilityCalls;check(context==&protectedProxy,"eligibility context lost through native ABI");return *static_cast<bool*>(context);}
using Caller=int (*)();
}
int main(){try{
    const Caller callers[]{neck,neckLater,neckRandom,bomb,back,backRandom};
    NativeGrabCallSites sites;
    for(size_t i=0;i<6;++i){check(callers[i]()==38+int(i),"unhooked native counter/return ABI incorrect");sites.returns[i]=observedReturn;}
    check(sites.valid(),"six genuine compiled return sites not distinct");
    for(auto at:sites.returns){check(sites.contains(at),"exact hold caller excluded");check(!sites.contains(at-1)&&!sites.contains(at+1),"neighboring unrelated instruction accepted");}
    auto bad=sites;bad.returns[5]=bad.returns[0];check(!bad.valid(),"ambiguous duplicate callers accepted");
    bad=sites;bad.returns[2]=0;check(!bad.valid(),"missing caller accepted");
    check(!NativeGrabCallSites{}.contains(0),"unverified default policy accepted a caller");
    NativeGrabHooks hooks;
    check(!hooks.installFixture(reinterpret_cast<uintptr_t>(originalCounter),bad,eligibility,&protectedProxy)&&!hooks.installed(),"incomplete signature proof installed a hook");
    check(hooks.installFixture(reinterpret_cast<uintptr_t>(originalCounter),sites,eligibility,&protectedProxy)&&hooks.installed(),"real x86 MinHook counter gateway failed to install");
    const auto callsAfterInstall=originalCalls;
    for(size_t i=0;i<6;++i)check(callers[i]()==38+int(i),"native/F2-off caller did not retain the original counter");
    check(originalCalls==callsAfterInstall+6,"native/F2-off counter was suppressed");
    protectedProxy=true;const auto beforeProtected=originalCalls;
    for(size_t i=0;i<6;++i)check(callers[i]()==128+int(i),"protected hold caller did not observe original completed escape value");
    check(originalCalls==beforeProtected,"protected holds still executed the original counter");
    const auto eligibleBeforeUnrelated=eligibilityCalls;
    check(unrelated()==44&&originalCalls==beforeProtected+1,"unrelated QTE/native caller was changed");
    check(eligibilityCalls==eligibleBeforeUnrelated,"unrelated caller invoked protection/health checks");
    // Sustained native CDECL round trips detect stack/result damage and verify
    // that switching eligibility restores original behavior immediately.
    for(unsigned round=0;round<1000;++round){protectedProxy=(round&1)!=0;for(size_t i=0;i<6;++i)check(callers[i]()==(protectedProxy?128:38)+int(i),"repeated ABI or eligibility transition failed");}
    hooks.removeFixture();protectedProxy=true;
    check(!hooks.installed()&&neck()==38&&unrelated()==44,"hook cleanup changed the restored native counter");
    std::printf("Native ordinary hold escape: %u checks passed, actual x86 MinHook gateway. No game process touched.\n",checks);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"Native grab fixture failed after %u checks: %s\n",checks,error.what());return 1;}}
