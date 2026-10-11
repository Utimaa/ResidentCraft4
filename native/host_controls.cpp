#include "host_controls.hpp"
#include "camera_control.hpp"
#include "hook_support.hpp"
#include "vendor/minhook/include/MinHook.h"
#include <array>
#include <cstdio>

namespace re4craft { namespace {
using PadRead=void(__cdecl*)();using Fire=BOOL(__cdecl*)();using Subscreen=bool(__cdecl*)(uint32_t,uint32_t);
PadRead originalPad{};Fire originalFireTrigger{},originalFireOn{};Subscreen originalSubscreen{};HostControls* controls{};
void __cdecl readPad(){originalPad();if(controls)controls->afterPadRead();}
BOOL __cdecl fireTrigger(){return controls&&controls->active()?FALSE:originalFireTrigger();}
BOOL __cdecl fireOn(){return controls&&controls->active()?FALSE:originalFireOn();}
bool __cdecl subscreen(uint32_t flags,uint32_t attributes){return controls&&blockNativeSubscreen(flags,controls->active())?false:originalSubscreen(flags,attributes);}
constexpr uint32_t HideCockpit=0x10000;
bool cockpitFlag(uintptr_t address,bool hidden){
    if(!hookdetail::memory(address,4,true))return false;
    __try{
        auto* flags=reinterpret_cast<volatile LONG*>(address);LONG value=*flags;
        for(;;){LONG wanted=hidden?value|HideCockpit:value&~HideCockpit;LONG old=InterlockedCompareExchange(flags,wanted,value);if(old==value)return true;value=old;}
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
void HostControls::setActive(bool active){lastActiveTime_.store(GetTickCount64(),std::memory_order_release);active_.store(active,std::memory_order_release);}
bool HostControls::active()const{return active_.load(std::memory_order_acquire)&&GetTickCount64()-lastActiveTime_.load(std::memory_order_acquire)<=750&&game_&&game_->sample().canReplace();}
bool HostControls::installHooks(GameAccess* game,LogFn log){
    if(installed_)return game_==game;if(!game||!game->initialized()||controls)return false;
    hookdetail::ImageCode code;if(!code.initialize())return false;
    uintptr_t pad=code.find("E8 ? ? ? ? E8 ? ? ? ? 68 ? ? ? ? 6A 04 E8 ? ? ? ? 83 C4 18 E8 ? ? ? ? E8 ? ? ? ? A1");
    uintptr_t trg=code.find("E8 ? ? ? ? 85 C0 74 ? 8B 96 D8 07 00 00 8B 4A 34 F6 81 55 03");
    uintptr_t on=code.find("E8 ? ? ? ? 85 C0 74 ? 8B 8E D8 07 00 00 8B 49 34 E8 ? ? ? ? 84 C0 0F ? ? ? ? ? 8B");
    uintptr_t subs=code.find("55 8B EC A1 ? ? ? ? B9 ? ? ? ? 85 88");
    uintptr_t held=code.find("A1 ? ? ? ? 33 C9 83 E0 ? 83 3D"),pressed=code.find("A1 ? ? ? ? 25 ? ? ? ? 0B C1 74 ? 88 0D");
    uintptr_t global=code.find("A1 ? ? ? ? B9 FF FF FF 7F 21 48 ? A1");
    std::array<uintptr_t,4> targets{code.callTarget(pad),code.callTarget(trg),code.callTarget(on),subs};
    for(auto target:targets)if(!target){if(log)log("Native input signatures missing or altered; control takeover disabled.");return false;}
    if(!held||!pressed||!global)return false;keyOn_=hookdetail::operand(held+1);keyTrigger_=hookdetail::operand(pressed+1);globalSlot_=hookdetail::operand(global+1);
    if(!hookdetail::memory(keyOn_,8,true)||!hookdetail::memory(keyTrigger_,8,true))return false;
    // Original routines must remain in the main executable, not an unknown detour.
    for(auto target:targets){uint8_t opcode{};if(!hookdetail::copy(target,&opcode,1)||opcode==0xe9)return false;}
    auto initialized=MH_Initialize();if(initialized!=MH_OK&&initialized!=MH_ERROR_ALREADY_INITIALIZED)return false;
    std::array<void*,4> detours{reinterpret_cast<void*>(readPad),reinterpret_cast<void*>(fireTrigger),reinterpret_cast<void*>(fireOn),reinterpret_cast<void*>(subscreen)};
    std::array<void**,4> originals{reinterpret_cast<void**>(&originalPad),reinterpret_cast<void**>(&originalFireTrigger),reinterpret_cast<void**>(&originalFireOn),reinterpret_cast<void**>(&originalSubscreen)};
    game_=game;log_=log;controls=this;size_t created=0;
    for(;created<targets.size();++created)if(MH_CreateHook(reinterpret_cast<void*>(targets[created]),detours[created],originals[created])!=MH_OK)break;
    if(created!=targets.size()){for(size_t i=0;i<created;++i)MH_RemoveHook(reinterpret_cast<void*>(targets[i]));controls=nullptr;return false;}
    size_t enabled=0;for(;enabled<targets.size();++enabled)if(MH_EnableHook(reinterpret_cast<void*>(targets[enabled]))!=MH_OK)break;
    if(enabled!=targets.size()){for(auto target:targets){MH_DisableHook(reinterpret_cast<void*>(target));MH_RemoveHook(reinterpret_cast<void*>(target));}controls=nullptr;return false;}
    installed_=true;if(log){char message[200]{};std::snprintf(message,sizeof(message),"Native input hooks installed: PadRead %08X, fire %08X/%08X, briefcase %08X.",static_cast<unsigned>(targets[0]),static_cast<unsigned>(targets[1]),static_cast<unsigned>(targets[2]),static_cast<unsigned>(targets[3]));log(message);}return true;
}
void HostControls::afterPadRead(){
    const bool requested=active_.load(std::memory_order_acquire);
    auto native=game_?game_->sample():NativeSnapshot{};
    const bool current=requested&&GetTickCount64()-lastActiveTime_.load(std::memory_order_acquire)<=750&&native.canReplace();
    // Capture native transitions at the input-poll boundary. The stop/status
    // fields and unfiltered key bits identify what changed before ownership
    // falls back. State-only logging is bounded and never runs under a mutex.
    struct DiagnosticKey {uint32_t phase,stop,status,be;uint8_t routine;};
    thread_local DiagnosticKey previous{};thread_local bool seen{},previousRequested{};
    thread_local uint64_t logWindow{};thread_local unsigned windowCount{},totalCount{};
    uint32_t phase=(requested?1U:0)|(current?2U:0)|(native.valid?4U:0)|(native.alive?8U:0)|(native.playing?16U:0)|(native.paused?32U:0)|(native.cutscene?64U:0);
    DiagnosticKey key{phase,native.stopFlags,native.statusFlags,native.playerBeFlags,native.routine};
    bool changed=!seen||key.phase!=previous.phase||key.stop!=previous.stop||key.status!=previous.status||key.be!=previous.be||key.routine!=previous.routine;
    if(log_&&changed&&(requested||previousRequested)&&totalCount<128){
        uint64_t now=GetTickCount64();if(now-logWindow>=1000){logWindow=now;windowCount=0;}
        if(windowCount<4){
            ++windowCount;++totalCount;char message[512]{};
            std::snprintf(message,sizeof(message),"Native input state changed: requested=%u active=%u valid=%u alive=%u playing=%u paused=%u cutscene=%u routine=%u stop=%08X (key=%u player=%u ctrlProcessing=%u) status=%08X playerBe=%08X HP=%d/%d keys=%016llX triggers=%016llX keyboardCtrl=%u.",requested,current,native.valid,native.alive,native.playing,native.paused,native.cutscene,unsigned(native.routine),native.stopFlags,(native.stopFlags&0x80000000)!=0,(native.stopFlags&0x10000000)!=0,(native.stopFlags&0x02000000)!=0,native.statusFlags,native.playerBeFlags,int(native.health),int(native.maxHealth),static_cast<unsigned long long>(native.keyOn),static_cast<unsigned long long>(native.keyTrigger),(GetAsyncKeyState(VK_CONTROL)&0x8000)!=0);
            log_(message);
        }
    }
    previous=key;previousRequested=requested;seen=true;
    if(current){
        bool gui=screenOpen_.load(std::memory_order_acquire);
        bool guest=guestAuthority_.load(std::memory_order_acquire);
        uint64_t held{},pressed{};if(hookdetail::copy(keyOn_,&held,8)){held=gui?0:filterGuestMovement(filterHostButtons(held,true),guest);hookdetail::write(keyOn_,&held,8);}if(hookdetail::copy(keyTrigger_,&pressed,8)){pressed=gui?0:filterGuestMovement(filterHostButtons(pressed,true),guest);hookdetail::write(keyTrigger_,&pressed,8);}
    }else setHudHidden(false);
    if(camera_)camera_->onInputPoll(!screenOpen_.load(std::memory_order_acquire));
}
bool HostControls::setHudHidden(bool hidden){
    std::lock_guard lock(hudMutex_);
    auto restoreBit=[&]{if(ownsHudBit_&&hookdetail::operand(globalSlot_)==hiddenHudGlobal_)cockpitFlag(hiddenHudGlobal_+0x58,originalHudHidden_);ownsHudBit_=false;hiddenHudGlobal_=0;};
    if(!hidden||!active()){restoreBit();return true;}
    auto native=game_->sample();if(!native.canReplace()){restoreBit();return false;}
    if(ownsHudBit_&&hiddenHudGlobal_!=native.globalAddress)restoreBit();
    uint32_t flags{};if(!hookdetail::copy(native.globalAddress+0x58,&flags,4))return false;
    if(!ownsHudBit_){hiddenHudGlobal_=native.globalAddress;originalHudHidden_=(flags&HideCockpit)!=0;ownsHudBit_=true;}
    return cockpitFlag(native.globalAddress+0x58,true);
}
}
