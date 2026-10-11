#pragma once
#include "beamng_mode.hpp"

namespace re4craft {
// The caller supplies freshly verified native character and gameplay state.
// This policy reads no game memory and grants no guest-mode authority.
struct LeonProtectionContext {
    BridgeMode mode{BridgeMode::Native};
    bool playable{},alive{},paused{},cutscene{};
    bool characterVerified{},isLeon{};
};

// Hooks can evaluate a separately published atomic request with fresh context,
// without reading the render thread's mutable hotkey policy object.
inline bool leonProtectionActive(bool requested,const LeonProtectionContext& context){
    return requested&&context.mode==BridgeMode::Native&&context.playable&&context.alive
        &&!context.paused&&!context.cutscene&&context.characterVerified&&context.isLeon;
}

// Selecting a guest mode protects the native proxy from grabs even during a
// source-loading or presentation hold. This grants only catch protection:
// no guest movement, damage, visibility or physics authority. The native
// processing pause flag is intentionally not an eligibility condition.
inline bool guestGrabProtectionActive(BridgeMode mode,bool nativeValid,bool alive,bool playing,bool cutscene){
    return guestBridgeMode(mode)
        &&nativeValid&&alive&&playing&&!cutscene;
}

class LeonProtectionPolicy {
public:
    bool requested()const{return requested_;}

    // Gameplay eligibility is separate from changing the user's request:
    // F2 can turn it off from a native menu or room transition. Focus is
    // required for the hotkey, while active protection may remain in effect
    // if native gameplay continues running in the background.
    bool handleF2Edge(BridgeMode mode,bool foreground,bool edge,bool textInputOpen=false){
        if(mode!=BridgeMode::Native||!foreground||!edge||textInputOpen)return false;
        requested_=!requested_;return true;
    }

    bool active(const LeonProtectionContext& context)const{
        return leonProtectionActive(requested_,context);
    }
private:
    bool requested_{};
};
}
