#include "native_grab_hooks.hpp"
#include "hook_support.hpp"
#include "vendor/minhook/include/MinHook.h"
#include <intrin.h>
#include <cstdio>

namespace re4craft { namespace {
NativeGrabHooks* owner{};
int __cdecl getCounter(){return owner->counter(reinterpret_cast<uintptr_t>(_ReturnAddress()));}
// Each unique caller must still call the exact independently resolved getter.
// Wildcards cover relocated globals and branch displacements, not thresholds
// or the native escape-state writes used to establish the caller's meaning.
constexpr const char* Getter=
    "A1 ? ? ? ? 8B 15 ? ? ? ? 80 BA 98 4F 00 00 03 8B 88 68 05 00 00 8B C1 73 07 99 2B C2 D1 F8 03 C1 C3";
constexpr const char* Neck=
    "E8 ? ? ? ? 8B 3D ? ? ? ? 83 F8 0A 72 ? 66 83 BF B4 4F 00 00 01 7F ? 68 C8 00 00 00 8D 8E BC 04 00 00 E8";
constexpr const char* NeckLater=
    "E8 ? ? ? ? 83 F8 1E 76 05 BF 0F 27 00 00 E8 ? ? ? ? 83 F8 14 76 0E E8 ? ? ? ? A8 01 74 05 BF 0F 27 00 00 F6 86 B0 04 00 00 80";
constexpr const char* Bomb=
    "E8 ? ? ? ? 83 F8 0F 0F 86 ? ? ? ? C6 86 FE 00 00 00 04 E9 ? ? ? ? FE C0 88 86 FE 00 00 00";
constexpr const char* Back=
    "E8 ? ? ? ? 83 F8 28 76 05 BF 0F 27 00 00 E8 ? ? ? ? 83 F8 1E 76 0E E8 ? ? ? ? A8 01 74 05 BF 0F 27 00 00 F6 86 B0 04 00 00 80";
}
bool NativeGrabCallSites::valid()const{
    for(size_t i=0;i<returns.size();++i){
        if(returns[i]<0x10000)return false;
        for(size_t j=0;j<i;++j)if(returns[i]==returns[j])return false;
    }
    return true;
}
bool NativeGrabCallSites::contains(uintptr_t caller)const{
    if(caller<0x10000)return false;
    for(auto at:returns)if(at==caller)return true;
    return false;
}
int NativeGrabHooks::counter(uintptr_t caller)const{
    // 127 is re4_tweaks' verified completed-gacha value. Original hold code
    // releases its own catch pointers/AI/camera; the bridge does not invent
    // actor state or remove unrelated attacks from Minecraft's damage relay.
    if(sites_.contains(caller)&&prevent_&&prevent_(context_))return 127;
    return original_();
}
bool NativeGrabHooks::install(Prevent prevent,void* context,LogFn log){
    if(installed())return true;
    hookdetail::ImageCode code;
    if(!code.initialize())return false;
    const auto getter=code.find(Getter),neck=code.find(Neck),later=code.find(NeckLater),bomb=code.find(Bomb),back=code.find(Back);
    if(!getter||!neck||!later||!bomb||!back){if(log)log("Ordinary hold escape signatures missing/altered; no grab hook installed.");return false;}
    const std::array<uintptr_t,6> calls{neck,later,later+15,bomb,back,back+15};
    NativeGrabCallSites sites;
    for(size_t i=0;i<calls.size();++i){
        if(code.callTarget(calls[i])!=getter){if(log)log("Ordinary hold escape caller altered; no grab hook installed.");return false;}
        sites.returns[i]=calls[i]+5;
    }
    return installVerified(getter,sites,prevent,context,log);
}
bool NativeGrabHooks::installVerified(uintptr_t target,const NativeGrabCallSites& sites,Prevent prevent,void* context,LogFn log){
    if(owner||installed()||!target||!prevent||!sites.valid())return false;
    auto initialized=MH_Initialize();
    if(initialized!=MH_OK&&initialized!=MH_ERROR_ALREADY_INITIALIZED)return false;
    if(MH_CreateHook(reinterpret_cast<void*>(target),reinterpret_cast<void*>(getCounter),reinterpret_cast<void**>(&original_))!=MH_OK)return false;
    sites_=sites;prevent_=prevent;context_=context;owner=this;
    if(MH_EnableHook(reinterpret_cast<void*>(target))!=MH_OK){
        MH_RemoveHook(reinterpret_cast<void*>(target));owner=nullptr;original_=nullptr;prevent_=nullptr;context_=nullptr;sites_={};return false;
    }
    target_=target;
    if(log){char line[240]{};std::snprintf(line,sizeof(line),"Ordinary Ganado hold escape installed: getter %08X, six exact neck/bomb/back callers; normal attacks retained.",unsigned(target));log(line);}
    return true;
}
#ifdef RE4CRAFT_GRAB_FIXTURE
bool NativeGrabHooks::installFixture(uintptr_t target,const NativeGrabCallSites& sites,Prevent prevent,void* context){
    return installVerified(target,sites,prevent,context,nullptr);
}
void NativeGrabHooks::removeFixture(){
    if(!target_)return;
    MH_DisableHook(reinterpret_cast<void*>(target_));MH_RemoveHook(reinterpret_cast<void*>(target_));
    target_=0;original_=nullptr;prevent_=nullptr;context_=nullptr;sites_={};owner=nullptr;
}
#endif
}
