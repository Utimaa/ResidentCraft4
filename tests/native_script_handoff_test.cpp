#include "../native/native_script_handoff.hpp"
#include <cstdio>
#include <stdexcept>
using namespace re4craft;
namespace {
unsigned checks{};
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
NativeTraversalState native(){NativeTraversalState s{};s.valid=s.alive=s.playing=true;
    s.globalAddress=0x123400;s.playerAddress=0x234500;s.playerGuid=456;s.room=0x50f;
    s.position={35000,3500,14000};return s;}
}
int main(){try{
    const auto start=native();auto landing=start;landing.position={32000,3500,15500};
    NativeScriptHandoff script;
    auto observe=[&](const NativeTraversalState& s,bool event,bool playable,uint64_t now,uint64_t tick=80){
        return script.observe(true,false,s,event,playable,7,tick,now,1000);};
    check(observe(start,true,false,1000)==WindowHandoffPhase::Idle,"F8 enabled inside a native event invented an old guest anchor");
    script.arm(false,start,7);check(observe(start,true,false,1100)==WindowHandoffPhase::Idle,"inactive source armed a scripted handoff");
    script.arm(true,start,7);
    check(observe(start,false,false,1200)==WindowHandoffPhase::Idle,"ordinary menu/focus pause became a scripted event");
    check(observe(start,true,false,1300)==WindowHandoffPhase::NativeAction,"actual STA_EVENT did not retain native placement");
    check(script.blocksGuestMotion(),"scripted action allowed stale source movement");
    check(observe(landing,true,false,65000)==WindowHandoffPhase::NativeAction,"long native intro inherited the window's 30-second deadline");
    check(observe(landing,false,false,65100)==WindowHandoffPhase::NativeAction,"paused native event completion granted source authority");
    check(observe(landing,false,true,65200,81)==WindowHandoffPhase::AwaitLanding,"STA_EVENT end did not retain actual native landing");
    check(script.landing().position.x==landing.position.x,"native script's real landing changed");
    check(!script.acknowledge(landing,7,81,landing.position,true,65210),"old source frame satisfied landing receipt");
    check(!script.acknowledge(landing,7,82,start.position,true,65210),"pre-intro source feet satisfied landing receipt");
    check(!script.acknowledge(landing,7,82,landing.position,false,65210),"ordinary MotionReady bypassed client/server receipt");
    check(script.acknowledge(landing,7,82,landing.position,true,65210),"fresh client/server landing failed");
    check(!script.blocksGuestMotion(),"acknowledged landing held source forever");
    check(observe(landing,true,false,65300)==WindowHandoffPhase::Idle,"second event acquired ownership before ordinary source movement resumed");
    for(unsigned changed=0;changed<7;++changed){script.cancel();script.arm(true,start,7);
        check(observe(start,true,false,1000)==WindowHandoffPhase::NativeAction,"fixture did not start real event");
        auto other=start;uint32_t epoch=7;
        if(changed==0)other.globalAddress+=16;if(changed==1)other.playerAddress+=16;
        if(changed==2)++other.playerGuid;if(changed==3)++other.room;if(changed==4)other.alive=false;
        if(changed==5)other.playing=false;if(changed==6)++epoch;
        check(script.observe(true,false,other,false,true,epoch,82,1100,1000)==WindowHandoffPhase::Failed,"changed script identity reopened guest authority");
        check(script.blocksGuestMotion(),"failed script identity admitted stale source pose");
    }
    script.cancel();script.arm(true,start,7);observe(start,true,false,1000);
    check(observe(start,true,false,181001)==WindowHandoffPhase::Failed,"orphaned script exceeded 180-second bound");
    script.cancel();script.arm(true,start,7);observe(start,true,false,1000);observe(landing,false,true,2000);
    check(observe(landing,false,true,4001)==WindowHandoffPhase::Failed,"missing landing receipt silently resumed");
    script.cancel();script.arm(true,start,7);observe(start,true,false,1000);observe(landing,false,true,2000);
    check(observe(landing,true,false,2100)==WindowHandoffPhase::NativeAction,"chained native event retained an obsolete landing target");
    auto secondLanding=landing;secondLanding.position.z+=1000;
    check(observe(secondLanding,false,true,2200,85)==WindowHandoffPhase::AwaitLanding,"second event did not retain its actual newer landing");
    check(!script.acknowledge(secondLanding,7,86,landing.position,true,2210),"first event's late source receipt acknowledged second event");
    check(script.acknowledge(secondLanding,7,86,secondLanding.position,true,2210),"chained event's fresh actual landing was not acknowledged");
    script.cancel();script.arm(true,start,7);observe(start,true,false,1000);
    check(script.observe(false,false,start,true,false,7,80,1100,1000)==WindowHandoffPhase::Idle,"disabled/disconnected mode retained script ownership");
    script.arm(true,start,7);
    check(script.observe(true,true,start,true,false,7,80,1200,1000)==WindowHandoffPhase::Idle,"exact native window competed with generic event cycle");
    script.arm(true,start,7);observe(start,true,false,1000);auto far=start;far.position.x+=32001;
    check(observe(far,false,true,1200)==WindowHandoffPhase::Failed,"script bypassed 32-block transfer bound");
    for(unsigned i=0;i<1000;++i){script.cancel();script.arm(true,start,7);
        check(observe(start,true,false,1000)==WindowHandoffPhase::NativeAction,"repeat event begin failed");
        check(observe(landing,false,true,2000,i+1)==WindowHandoffPhase::AwaitLanding,"repeat actual landing failed");
        check(script.acknowledge(landing,7,i+2,landing.position,true,2010),"repeat explicit source receipt failed");
    }
    std::printf("Native STA_EVENT handoff: %u x86 policy checks passed; retained source anchor, exact event transition, real landing receipt, identity/deadline/fallback guards. No live encounter executed.\n",checks);
    return 0;
}catch(const std::exception& error){std::fprintf(stderr,"Native script handoff failed after %u checks: %s\n",checks,error.what());return 1;}}
