#pragma once
#include "game_access.hpp"
#include "beamng_mode.hpp"
#include <cmath>

namespace re4craft {
enum class WindowHandoffPhase : uint8_t { Idle, NativeAction, AwaitLanding, Failed };
// Receipt policy shared by the exact window callback and the separately scoped
// STA_EVENT cycle observer. Neither caller infers an action from displacement.
class NativeWindowHandoff {
public:
    static constexpr uint64_t ActionLimitMs=30000,LandingLimitMs=2000;
    uint64_t begin(BridgeMode mode,bool guestRequested,const NativeTraversalState& native,
                   uint32_t epoch,uint64_t now,float unitsPerBlock,uint64_t actionLimitMs=ActionLimitMs){
        if(mode!=BridgeMode::Minecraft||!guestRequested||phase_!=WindowHandoffPhase::Idle
           ||!eligible(native)||!epoch||!std::isfinite(unitsPerBlock)||unitsPerBlock<=0
           ||!actionLimitMs||actionLimitMs>180000)return 0;
        if(++serial_==0)++serial_;
        token_=serial_;epoch_=epoch;identity_=native;started_=now;units_=unitsPerBlock;actionLimit_=actionLimitMs;
        phase_=WindowHandoffPhase::NativeAction;return token_;
    }
    bool complete(uint64_t token,bool originalSucceeded,const NativeTraversalState& native,
                  uint64_t sourceFrame,uint64_t now){
        if(!token||token!=token_||phase_!=WindowHandoffPhase::NativeAction)return false;
        if(!originalSucceeded||!sameIdentity(native)||!within(started_,now,actionLimit_)){
            phase_=WindowHandoffPhase::Failed;return false;
        }
        const double dx=double(native.position.x)-identity_.position.x;
        const double dy=double(native.position.y)-identity_.position.y;
        const double dz=double(native.position.z)-identity_.position.z;
        if(dx*dx+dy*dy+dz*dz>double(units_)*units_*1024){phase_=WindowHandoffPhase::Failed;return false;}
        landing_=native;landingAt_=now;sourceFrame_=sourceFrame;
        phase_=WindowHandoffPhase::AwaitLanding;return true;
    }
    WindowHandoffPhase phase(const NativeTraversalState& native,uint32_t epoch,uint64_t now){
        if(phase_==WindowHandoffPhase::Idle||phase_==WindowHandoffPhase::Failed)return phase_;
        const bool timed=phase_==WindowHandoffPhase::NativeAction?within(started_,now,actionLimit_):within(landingAt_,now,LandingLimitMs);
        if(epoch!=epoch_||!sameIdentity(native)||!timed)phase_=WindowHandoffPhase::Failed;
        return phase_;
    }
    // The caller must supply fresh wire motion carrying the explicit source
    // traversal receipt; a coincidentally matching ordinary pose is insufficient.
    bool acknowledge(const NativeTraversalState& native,uint32_t epoch,uint64_t frame,
                     Vec3 sourceNativeFeet,bool sourceReceiptFresh,uint64_t now){
        if(phase(native,epoch,now)!=WindowHandoffPhase::AwaitLanding||!sourceReceiptFresh
           ||frame<=sourceFrame_||!finite(sourceNativeFeet))return false;
        const double dx=double(sourceNativeFeet.x)-landing_.position.x;
        const double dy=double(sourceNativeFeet.y)-landing_.position.y;
        const double dz=double(sourceNativeFeet.z)-landing_.position.z;
        const double tolerance=double(units_)*.125;
        if(dx*dx+dy*dy+dz*dz>tolerance*tolerance)return false;
        cancel();return true;
    }
    bool blocksGuestMotion()const{return phase_!=WindowHandoffPhase::Idle;}
    WindowHandoffPhase currentPhase()const{return phase_;}
    uint32_t epoch()const{return epoch_;}
    const NativeTraversalState& landing()const{return landing_;}
    void cancel(){phase_=WindowHandoffPhase::Idle;token_=0;epoch_=0;identity_={};landing_={};started_=landingAt_=sourceFrame_=0;units_=0;}
private:
    static bool finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)
        &&std::abs(p.x)<1e8f&&std::abs(p.y)<1e8f&&std::abs(p.z)<1e8f;}
    static bool eligible(const NativeTraversalState& s){return s.valid&&s.alive&&s.playing
        &&s.globalAddress>=0x10000&&s.playerAddress>=0x10000&&finite(s.position)&&finite(s.angles);}
    static bool within(uint64_t start,uint64_t now,uint64_t duration){return now>=start&&now-start<=duration;}
    bool sameIdentity(const NativeTraversalState& s)const{return eligible(s)
        &&s.globalAddress==identity_.globalAddress&&s.playerAddress==identity_.playerAddress
        &&s.playerGuid==identity_.playerGuid&&s.room==identity_.room;}
    WindowHandoffPhase phase_{WindowHandoffPhase::Idle};
    NativeTraversalState identity_{},landing_{};
    uint64_t serial_{},token_{},started_{},landingAt_{},sourceFrame_{};
    uint32_t epoch_{};float units_{};uint64_t actionLimit_{ActionLimitMs};
};
}
