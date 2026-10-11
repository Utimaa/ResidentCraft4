#include "../native/native_window_handoff.hpp"
#include <cstdio>
#include <stdexcept>
#include <limits>
using namespace re4craft;
namespace {
unsigned checks{};
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
NativeTraversalState native(){NativeTraversalState s{};s.valid=s.alive=s.playing=true;
    s.globalAddress=0x123400;s.playerAddress=0x234500;s.playerGuid=456;s.room=0x100;
    s.position={200,400,600};s.angles={0,1,0};return s;}
}
int main(){try{
    auto start=native(),land=start;land.position={500,-300,700};
    NativeWindowHandoff handoff;
    check(!handoff.blocksGuestMotion(),"idle blocked ordinary Minecraft physics");
    for(auto mode:{BridgeMode::Native,BridgeMode::BeamNG,BridgeMode::Halo,BridgeMode::Ultrakill})
        check(!handoff.begin(mode,true,start,8,1000,100),"a non-Minecraft mode gained native window authority");
    check(!handoff.begin(BridgeMode::Minecraft,false,start,8,1000,100),"inactive Minecraft gained authority");
    check(!handoff.begin(BridgeMode::Minecraft,true,start,0,1000,100),"unknown epoch gained authority");
    for(auto units:{0.f,-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
        check(!handoff.begin(BridgeMode::Minecraft,true,start,8,1000,units),"invalid units gained authority");
    for(unsigned field=0;field<7;++field){auto bad=start;
        if(field==0)bad.valid=false;if(field==1)bad.alive=false;if(field==2)bad.playing=false;
        if(field==3)bad.globalAddress=0;if(field==4)bad.playerAddress=0;
        if(field==5)bad.position.x=std::numeric_limits<float>::quiet_NaN();
        if(field==6)bad.angles.y=std::numeric_limits<float>::infinity();
        check(!handoff.begin(BridgeMode::Minecraft,true,bad,8,1000,100),"invalid/dead/non-game identity began traversal");
    }
    auto token=handoff.begin(BridgeMode::Minecraft,true,start,8,1000,100);
    check(token&&handoff.blocksGuestMotion(),"verified window failed to retain native motion");
    check(!handoff.begin(BridgeMode::Minecraft,true,start,8,1010,100),"nested action overwrote native ownership");
    check(handoff.phase(start,8,1600)==WindowHandoffPhase::NativeAction,"original running action lost ownership");
    check(!handoff.complete(token+1,true,land,55,1600),"wrong callback token completed action");
    check(handoff.complete(token,true,land,55,1600),"verified original landing not retained");
    check(handoff.currentPhase()==WindowHandoffPhase::AwaitLanding&&handoff.blocksGuestMotion(),"source resumed before landing receipt");
    check(handoff.landing().position.y==-300&&handoff.epoch()==8,"actual landing/epoch changed");
    check(!handoff.acknowledge(land,8,55,land.position,true,1610),"pre-landing motion acknowledged");
    check(!handoff.acknowledge(land,8,56,land.position,false,1610),"ordinary untagged motion acknowledged");
    auto far=land.position;far.y+=13;
    check(!handoff.acknowledge(land,8,56,far,true,1610),"wrong vertical landing acknowledged");
    auto invalid=land.position;invalid.x=std::numeric_limits<float>::quiet_NaN();
    check(!handoff.acknowledge(land,8,56,invalid,true,1610),"non-finite landing acknowledged");
    check(handoff.acknowledge(land,8,56,land.position,true,1610)&&!handoff.blocksGuestMotion(),"explicit fresh source landing did not release authority");
    check(!handoff.complete(token,true,land,57,1620),"stale callback recreated completed authority");
    for(unsigned field=0;field<9;++field){handoff.cancel();token=handoff.begin(BridgeMode::Minecraft,true,start,8,2000,100);
        auto changed=land;uint32_t epoch=8;
        if(field==0)changed.globalAddress+=16;if(field==1)changed.playerAddress+=16;
        if(field==2)changed.playerGuid+=1;if(field==3)changed.room+=1;
        if(field==4)changed.valid=false;if(field==5)changed.alive=false;
        if(field==6)changed.playing=false;if(field==7)epoch=9;
        if(field==8)changed.position.z=std::numeric_limits<float>::infinity();
        check(handoff.phase(changed,epoch,2010)==WindowHandoffPhase::Failed,"changed actor/room/death/epoch reused native action");
        check(handoff.blocksGuestMotion(),"failed identity allowed stale source writes");
        check(!handoff.complete(token,true,land,55,2020),"late complete resurrected failed handoff");
    }
    handoff.cancel();token=handoff.begin(BridgeMode::Minecraft,true,start,8,4000,100);
    check(!handoff.complete(token,false,land,60,5000)&&handoff.blocksGuestMotion(),"unsuccessful native action granted source landing");
    handoff.cancel();token=handoff.begin(BridgeMode::Minecraft,true,start,8,4000,100);
    check(handoff.phase(start,8,34001)==WindowHandoffPhase::Failed,"orphaned native coroutine exceeded finite deadline");
    handoff.cancel();token=handoff.begin(BridgeMode::Minecraft,true,start,8,4000,100);
    auto tooFar=land;tooFar.position.y=start.position.y+3201;
    check(!handoff.complete(token,true,tooFar,60,5000),"native action bypassed existing 32-block transfer bound");
    handoff.cancel();token=handoff.begin(BridgeMode::Minecraft,true,start,8,4000,100);
    check(handoff.complete(token,true,land,60,5000),"new bounded landing failed");
    check(handoff.phase(land,8,7001)==WindowHandoffPhase::Failed,"missing source receipt silently resumed after timeout");
    check(!handoff.acknowledge(land,8,61,land.position,true,7002),"late source receipt reopened failed authority");
    for(unsigned i=0;i<1000;++i){handoff.cancel();token=handoff.begin(BridgeMode::Minecraft,true,start,8,10000,100);
        check(token!=0,"repeat start lost monotonic token");
        check(handoff.phase(start,8,11000)==WindowHandoffPhase::NativeAction,"repeat native action not protected");
        check(handoff.complete(token,true,land,100+i,12000),"repeat exact landing failed");
        check(!handoff.acknowledge(land,8,100+i,land.position,true,12010),"repeat stale frame acknowledged");
        check(handoff.acknowledge(land,8,101+i,land.position,true,12020),"repeat fresh landing did not resume");
    }
    std::printf("Native window handoff: %u x86 policy checks passed; exact callback/landing/receipt guards.\n",checks);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"Window handoff failed after %u checks: %s\n",checks,error.what());return 1;}}
