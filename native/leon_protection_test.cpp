#include "leon_protection.hpp"
#include <cstdio>
#include <stdexcept>

using namespace re4craft;
namespace {
unsigned checks{};
void check(bool passed,const char* message){++checks;if(!passed)throw std::runtime_error(message);}
LeonProtectionContext playableLeon(){
    LeonProtectionContext context;
    context.playable=context.alive=context.characterVerified=context.isLeon=true;
    return context;
}
}
int main(){try{
    LeonProtectionPolicy policy;
    auto context=playableLeon();
    check(!policy.requested()&&!policy.active(context),"protection was enabled by default");
    check(!policy.active({}),"default unverified context granted protection");
    check(policy.handleF2Edge(BridgeMode::Native,true,true)&&policy.requested(),"foreground native F2 did not enable the request");
    check(policy.active(context),"requested protection rejected verified playable Leon");
    check(!policy.handleF2Edge(BridgeMode::Native,true,false)&&policy.requested(),"a held key without a new edge changed the request");
    check(!policy.handleF2Edge(BridgeMode::Native,false,true)&&policy.requested(),"background F2 changed the request");
    check(!policy.handleF2Edge(BridgeMode::Native,true,true,true)&&policy.requested(),"native text input changed the request");

    for(auto mode:{BridgeMode::Minecraft,BridgeMode::BeamNG,BridgeMode::Halo,BridgeMode::Ultrakill,static_cast<BridgeMode>(99)}){
        context.mode=mode;
        check(!policy.active(context),"a guest or unknown mode gained native Leon protection");
        check(!policy.handleF2Edge(mode,true,true)&&policy.requested(),"guest/unknown F2 changed the retained native request");
    }
    context=playableLeon();
    check(policy.active(context),"returning to native mode lost the retained request");
    context.playable=false;context.alive=false;context.paused=true;context.cutscene=true;
    context.characterVerified=false;context.isLeon=false;
    check(!policy.active(context)&&policy.requested(),"room/menu eligibility changed the retained request or protected an ineligible actor");
    check(policy.handleF2Edge(BridgeMode::Native,true,true)&&!policy.requested(),"F2 could not disable protection outside playable gameplay");
    check(!policy.active(playableLeon()),"disabled request resumed on return to gameplay");
    check(policy.handleF2Edge(BridgeMode::Native,true,true)&&policy.requested(),"request could not be enabled before a playable room");
    context=playableLeon();
    check(policy.active(context),"room transition lost the enabled request");

    // Exhaust eligibility independently of the hotkey. No health value,
    // room identifier or fallback actor can override a missing guard.
    for(auto mode:{BridgeMode::Native,BridgeMode::Minecraft,BridgeMode::BeamNG,BridgeMode::Halo,BridgeMode::Ultrakill,static_cast<BridgeMode>(99)})
        for(unsigned flags=0;flags<64;++flags){
            context={mode,bool(flags&1),bool(flags&2),bool(flags&4),bool(flags&8),bool(flags&16),bool(flags&32)};
            const bool expected=mode==BridgeMode::Native&&(flags&3)==3&&!(flags&12)&&(flags&48)==48;
            check(policy.active(context)==expected,"eligibility combination bypassed a native/Leon/alive/playable/paused/cutscene guard");
            check(leonProtectionActive(true,context)==expected,"published request helper disagreed with the eligibility policy");
        }
    check(policy.handleF2Edge(BridgeMode::Native,true,true)&&!policy.requested(),"final native F2 did not disable the request");
    for(auto mode:{BridgeMode::Native,BridgeMode::Minecraft,BridgeMode::BeamNG,BridgeMode::Halo,BridgeMode::Ultrakill,static_cast<BridgeMode>(99)})
        for(unsigned flags=0;flags<64;++flags){
            context={mode,bool(flags&1),bool(flags&2),bool(flags&4),bool(flags&8),bool(flags&16),bool(flags&32)};
            check(!policy.active(context),"disabled request granted protection through a context combination");
            check(!leonProtectionActive(false,context),"false published request granted protection");
        }

    // Verify the change guard for both on and off requests; no new foreground
    // native edge means no modification, regardless of room eligibility.
    for(bool initiallyRequested:{false,true})for(unsigned flags=0;flags<8;++flags){
        LeonProtectionPolicy hotkey;
        if(initiallyRequested)hotkey.handleF2Edge(BridgeMode::Native,true,true);
        const bool foreground=flags&1,edge=flags&2,textInput=flags&4;
        const bool allowed=foreground&&edge&&!textInput;
        check(hotkey.handleF2Edge(BridgeMode::Native,foreground,edge,textInput)==allowed,"native hotkey change guard was inconsistent");
        check(hotkey.requested()==(initiallyRequested!=allowed),"blocked hotkey changed the request or allowed edge was lost");
    }

    // A loading or stalled guest still needs its native proxy protected.
    // This decision deliberately has no source freshness, draw or F2 input.
    LeonProtectionPolicy independent;
    for(bool f2Requested:{false,true}){
        if(f2Requested)check(independent.handleF2Edge(BridgeMode::Native,true,true),"could not prepare native-only F2 independence case");
        for(auto guest:GuestBridgeModes){
            check(guestGrabProtectionActive(guest,true,true,true,false),"selected guest proxy was not protected during its loading/hold state");
            check(!independent.active({guest,true,true,false,false,true,true}),"automatic guest grab protection enabled native F2 health policy");
            check(!independent.handleF2Edge(guest,true,true)&&independent.requested()==f2Requested,"guest F2 changed the retained native request");
            check(!guestGrabProtectionActive(guest,false,true,true,false),"unverified native proxy gained guest catch protection");
            check(!guestGrabProtectionActive(guest,true,false,true,false),"dead proxy gained guest catch protection");
            check(!guestGrabProtectionActive(guest,true,true,false,false),"native menu gained guest catch protection");
            check(!guestGrabProtectionActive(guest,true,true,true,true),"native cutscene gained guest catch protection");
        }
    }
    check(!guestGrabProtectionActive(BridgeMode::Native,true,true,true,false),"automatic guest guard remained active after returning to native RE4");
    check(independent.active(playableLeon()),"return to native lost the independent F2 request");
    check(independent.handleF2Edge(BridgeMode::Native,true,true)&&!independent.requested(),"native F2 could not turn off its independent request");
    check(guestGrabProtectionActive(BridgeMode::Minecraft,true,true,true,false)
        &&guestGrabProtectionActive(BridgeMode::BeamNG,true,true,true,false),"turning native F2 off disabled automatic guest catch protection");

    for(auto mode:{BridgeMode::Native,BridgeMode::Minecraft,BridgeMode::BeamNG,BridgeMode::Halo,BridgeMode::Ultrakill,static_cast<BridgeMode>(99)})
        for(unsigned flags=0;flags<16;++flags){
            const bool valid=flags&1,alive=flags&2,playing=flags&4,cutscene=flags&8;
            const bool expected=(mode==BridgeMode::Minecraft||mode==BridgeMode::BeamNG||mode==BridgeMode::Halo||mode==BridgeMode::Ultrakill)&&(flags&7)==7&&!cutscene;
            check(guestGrabProtectionActive(mode,valid,alive,playing,cutscene)==expected,"automatic guest catch guard bypassed mode/native/alive/playing/cutscene eligibility");
        }
    std::printf("Leon protection policy: %u checks passed. No game process or health memory accessed.\n",checks);
    return 0;
}catch(const std::exception& error){std::fprintf(stderr,"Leon protection policy failed after %u checks: %s\n",checks,error.what());return 1;}}
