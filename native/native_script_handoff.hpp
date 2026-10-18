#pragma once
#include "native_window_handoff.hpp"

namespace re4craft {
// Native event begin/end (GLOBAL_WK::STA_EVENT) owns scripted placements. The
// source deliberately retains its transform during pause, so same-room events
// require the same real client/server landing receipt as a native window.
class NativeScriptHandoff {
public:
    static constexpr uint64_t ActionLimitMs=180000;
    void arm(bool actualGuestMovement,const NativeTraversalState& state,uint32_t epoch){
        if(!actualGuestMovement||receipt_.blocksGuestMotion()||!valid(state)||!epoch)return;
        armed_=true;identity_=state;epoch_=epoch;
    }
    WindowHandoffPhase observe(bool enabled,bool windowOwnsMotion,const NativeTraversalState& state,
            bool nativeEvent,bool nativePlayable,uint32_t epoch,uint64_t sourceFrame,uint64_t now,float units){
        if(!enabled||windowOwnsMotion){cancel();return WindowHandoffPhase::Idle;}
        if(armed_&&(!sameIdentity(state)||epoch!=epoch_)){
            // An existing event must fail closed; a mere old arm can retire.
            if(!receipt_.blocksGuestMotion()){cancel();return WindowHandoffPhase::Idle;}
        }
        if(receipt_.blocksGuestMotion()){
            const auto phase=receipt_.phase(state,epoch,now);
            // A second real event can start before Minecraft has acknowledged
            // the first landing. Keep native ownership and capture its newer
            // landing; never send the first event's stale target afterward.
            if(phase==WindowHandoffPhase::AwaitLanding&&nativeEvent){
                receipt_.cancel();
                token_=receipt_.begin(BridgeMode::Minecraft,true,state,epoch,now,units,ActionLimitMs);
                return receipt_.currentPhase();
            }
            if(phase==WindowHandoffPhase::NativeAction&&!nativeEvent&&nativePlayable)
                receipt_.complete(token_,true,state,sourceFrame,now);
            return receipt_.currentPhase();
        }
        if(armed_&&nativeEvent&&sameIdentity(state)&&epoch==epoch_){
            token_=receipt_.begin(BridgeMode::Minecraft,true,state,epoch,now,units,ActionLimitMs);
        }
        return receipt_.currentPhase();
    }
    bool acknowledge(const NativeTraversalState& state,uint32_t epoch,uint64_t frame,
            Vec3 feet,bool fresh,uint64_t now){
        if(!receipt_.acknowledge(state,epoch,frame,feet,fresh,now))return false;
        armed_=false;token_=0;identity_={};epoch_=0;return true;
    }
    bool blocksGuestMotion()const{return receipt_.blocksGuestMotion();}
    WindowHandoffPhase currentPhase()const{return receipt_.currentPhase();}
    const NativeTraversalState& landing()const{return receipt_.landing();}
    void cancel(){receipt_.cancel();armed_=false;token_=0;epoch_=0;identity_={};}
private:
    static bool valid(const NativeTraversalState& state){return state.valid&&state.alive&&state.playing
        &&state.globalAddress>=0x10000&&state.playerAddress>=0x10000;}
    bool sameIdentity(const NativeTraversalState& state)const{return valid(state)
        &&state.globalAddress==identity_.globalAddress&&state.playerAddress==identity_.playerAddress
        &&state.playerGuid==identity_.playerGuid&&state.room==identity_.room;}
    NativeWindowHandoff receipt_;
    NativeTraversalState identity_{};
    bool armed_{};uint32_t epoch_{};uint64_t token_{};
};
}
