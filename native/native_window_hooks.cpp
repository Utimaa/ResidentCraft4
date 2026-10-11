#include "native_window_hooks.hpp"
#include "hook_support.hpp"
#include "vendor/minhook/include/MinHook.h"
#include <cstdio>

namespace re4craft {namespace {
NativeWindowHooks* owner{};
void* originalSuccess{};
bool __cdecl windowCallback(void* window){return owner->invoke(window);}
void __cdecl completedOriginalWindow(void* window){if(owner)owner->observeOriginalSuccess(window);}
// The original cooperative scheduler can resume directly into its callback
// stack, bypassing the C++ caller after original_(). Observe its exact verified
// success epilogue, preserving every register/flag and the original AL/RET.
__declspec(naked) void originalWindowSuccess(){
    __asm { pushfd }
    __asm { pushad }
    __asm { mov eax, dword ptr [esp+8] }
    __asm { push dword ptr [eax+8] }
    __asm { call completedOriginalWindow }
    __asm { add esp, 4 }
    __asm { popad }
    __asm { popfd }
    __asm { jmp dword ptr [originalSuccess] }
}
bool actualWindow(void* window){
    uint8_t bytes[0x104]{};uint32_t flags{},vtable{};
    if(!hookdetail::copy(reinterpret_cast<uintptr_t>(window),bytes,sizeof(bytes)))return false;
    std::memcpy(&flags,bytes+4,4);std::memcpy(&vtable,bytes,4);
    return vtable>=0x10000&&(flags&0x201U)==1U&&bytes[0x100]==0x56;
}
}
bool NativeWindowHooks::invoke(void* window){
    const uint64_t token=actualWindow(window)&&begin_?begin_(context_):0;
    if(token){pendingWindow_.store(reinterpret_cast<uintptr_t>(window),std::memory_order_relaxed);pendingToken_.store(token,std::memory_order_release);}
    const bool success=original_(window);
    uint64_t expected=token;
    if(token&&pendingToken_.compare_exchange_strong(expected,0,std::memory_order_acq_rel)&&complete_){pendingWindow_=0;complete_(context_,token,success);}
    return success;
}
void NativeWindowHooks::observeOriginalSuccess(void* window){
    if(pendingWindow_.load(std::memory_order_acquire)!=reinterpret_cast<uintptr_t>(window))return;
    const auto token=pendingToken_.exchange(0,std::memory_order_acq_rel);
    if(token&&complete_){pendingWindow_=0;complete_(context_,token,true);}
}
bool NativeWindowHooks::install(Begin begin,Complete complete,void* context,LogFn log){
    if(installed())return true;
    hookdetail::ImageCode code;if(!code.initialize())return false;
    const uintptr_t setter=code.find(windowdetail::Setter),callback=code.find(windowdetail::Callback);
    const uintptr_t thunk=setter?hookdetail::operand(setter+14):0;
    // Do not recognise a general action flag or arbitrary coroutine. The
    // original window setter must point directly to this exact callback thunk.
    uint8_t tail[0x17]{};
    constexpr uint8_t prefix[]{0x8b,0x4d,0xfc,0x5e,0x5b,0x33,0xcd,0xb0,0x01,0x5f,0xe8};
    if(!setter||!callback||code.branch(thunk)!=callback
       ||!code.contains(callback+0x851,sizeof(tail))
       ||!hookdetail::copy(callback+0x851,tail,sizeof(tail))
       ||std::memcmp(tail,prefix,sizeof(prefix))||tail[15]!=0x8b||tail[16]!=0xe5
       ||tail[17]!=0x5d||tail[18]!=0xc3){
        if(log)log("Exact window traversal callback missing/altered; native window handoff unavailable.");return false;
    }
    return installVerified(callback,callback+0x858,begin,complete,context,log);
}
bool NativeWindowHooks::installVerified(uintptr_t target,uintptr_t successTarget,Begin begin,Complete complete,void* context,LogFn log){
    if(owner||installed()||!target||!begin||!complete)return false;
    const auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return false;
    if(MH_CreateHook(reinterpret_cast<void*>(target),reinterpret_cast<void*>(windowCallback),reinterpret_cast<void**>(&original_))!=MH_OK)return false;
    if(successTarget&&MH_CreateHook(reinterpret_cast<void*>(successTarget),reinterpret_cast<void*>(originalWindowSuccess),&originalSuccess)!=MH_OK){
        MH_RemoveHook(reinterpret_cast<void*>(target));original_=nullptr;return false;
    }
    begin_=begin;complete_=complete;context_=context;owner=this;
    if((successTarget&&MH_EnableHook(reinterpret_cast<void*>(successTarget))!=MH_OK)||MH_EnableHook(reinterpret_cast<void*>(target))!=MH_OK){
        if(successTarget){MH_DisableHook(reinterpret_cast<void*>(successTarget));MH_RemoveHook(reinterpret_cast<void*>(successTarget));}
        MH_RemoveHook(reinterpret_cast<void*>(target));owner=nullptr;original_=nullptr;begin_=nullptr;complete_=nullptr;context_=nullptr;return false;
    }
    target_=target;successTarget_=successTarget;
    if(log){char line[224]{};std::snprintf(line,sizeof(line),"Exact native window JumpOut/JumpIn entry %08X and original success observer %08X installed; cooperative completion and landing retained.",unsigned(target),unsigned(successTarget));log(line);}
    return true;
}
#ifdef RE4CRAFT_WINDOW_FIXTURE
bool NativeWindowHooks::installFixture(uintptr_t target,Begin begin,Complete complete,void* context,uintptr_t successTarget){return installVerified(target,successTarget,begin,complete,context,nullptr);}
void NativeWindowHooks::removeFixture(){
    if(!target_)return;MH_DisableHook(reinterpret_cast<void*>(target_));MH_RemoveHook(reinterpret_cast<void*>(target_));
    if(successTarget_){MH_DisableHook(reinterpret_cast<void*>(successTarget_));MH_RemoveHook(reinterpret_cast<void*>(successTarget_));}
    target_=successTarget_=0;original_=nullptr;originalSuccess=nullptr;begin_=nullptr;complete_=nullptr;context_=nullptr;pendingToken_=0;pendingWindow_=0;owner=nullptr;
}
#endif
}
