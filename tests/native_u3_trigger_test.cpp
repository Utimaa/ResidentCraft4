// Actual retail AEV geometry executes only in this fixture process. The
// original instructions and room record are generated privately under build/.
#include <mutex>
#include <atomic>
#include <vector>
#include <algorithm>
#include <cmath>
#define private public
#include "../native/guest_controller.hpp"
#undef private
#include "../build/native-u3-fixture-private.hpp"
#include <windows.h>
#include <array>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
using namespace re4craft;
namespace {
unsigned checks{};
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
void __fastcall cookie(uint32_t){}
void unexpected(){std::abort();}
struct Block {uint32_t original;uint8_t* code;size_t size;};
class OriginalTrigger {
    std::vector<Block> blocks_;uint32_t cookie_{};
    std::array<uint64_t,std::size(u3fixture::Constants)> constants_{};
    uintptr_t address(uint32_t original){
        for(const auto& block:blocks_)if(original>=block.original&&original<block.original+block.size)
            return reinterpret_cast<uintptr_t>(block.code)+original-block.original;
        if(original==0xc35e10)return reinterpret_cast<uintptr_t>(&cookie_);
        for(size_t i=0;i<std::size(u3fixture::Constants);++i)
            if(u3fixture::Constants[i].address==original)return reinterpret_cast<uintptr_t>(&constants_[i]);
        if(original==0xa6cd9a)return reinterpret_cast<uintptr_t>(cookie);
        return reinterpret_cast<uintptr_t>(unexpected);
    }
    template<size_t N>void allocate(uint32_t start,const uint8_t(&bytes)[N]){
        auto* data=static_cast<uint8_t*>(VirtualAlloc(nullptr,N,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        check(data!=nullptr,"cannot allocate private original AEV oracle");std::memcpy(data,bytes,N);blocks_.push_back({start,data,N});
    }
    template<class C,class A>void patch(size_t which,const C& calls,const A& absolute){
        auto& block=blocks_.at(which);
        for(const auto& p:calls){const auto at=block.code+p.offset;
            const int32_t displacement=static_cast<int32_t>(address(p.target)-reinterpret_cast<uintptr_t>(at+5));std::memcpy(at+1,&displacement,4);}
        for(const auto& p:absolute){const uint32_t value=static_cast<uint32_t>(address(p.target));std::memcpy(block.code+p.offset,&value,4);}
        DWORD old{};check(VirtualProtect(block.code,block.size,PAGE_EXECUTE_READ,&old)!=0,"cannot protect original AEV oracle");
        check(FlushInstructionCache(GetCurrentProcess(),block.code,block.size)!=0,"cannot flush original AEV oracle");
    }
public:
    OriginalTrigger(){
        for(size_t i=0;i<constants_.size();++i)constants_[i]=u3fixture::Constants[i].bits;
        allocate(u3fixture::CopyStart,u3fixture::CopyCode);allocate(u3fixture::PredicateStart,u3fixture::PredicateCode);
        allocate(u3fixture::DispatchStart,u3fixture::DispatchCode);allocate(u3fixture::QuadStart,u3fixture::QuadCode);
        patch(0,u3fixture::CopyCalls,u3fixture::CopyAbsolute);patch(1,u3fixture::PredicateCalls,u3fixture::PredicateAbsolute);
        patch(2,u3fixture::DispatchCalls,u3fixture::DispatchAbsolute);patch(3,u3fixture::QuadCalls,u3fixture::QuadAbsolute);
    }
    ~OriginalTrigger(){for(auto& block:blocks_)VirtualFree(block.code,0,MEM_RELEASE);}
    float centerOffset(){for(const auto& c:u3fixture::Constants)if(c.address==0xb0fc38){double value{};std::memcpy(&value,&c.bits,8);return float(value);}throw std::runtime_error("missing original player AEV center offset");}
    void* predicate(){return reinterpret_cast<void*>(address(u3fixture::PredicateStart));}
};
// Actual 6B7DB0 uses ESI=AEV, ECX=center, EDI=front and one cdecl actor.
__declspec(naked) int invoke(void*,void*,const Vec3*,const Vec3*,void*){
    __asm {
        push ebp
        mov ebp,esp
        push ebx
        push esi
        push edi
        mov eax,[ebp+8]
        mov esi,[ebp+12]
        mov ecx,[ebp+16]
        mov edi,[ebp+20]
        push dword ptr [ebp+24]
        call eax
        add esp,4
        and eax,255
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
template<class T>void field(uint8_t* data,size_t at,T value){std::memcpy(data+at,&value,sizeof(value));}
}
int main(){try{
    OriginalTrigger original;std::array<uint8_t,0x9c> aev{};std::memcpy(aev.data(),u3fixture::AreaEvent3,aev.size());
    std::array<uint8_t,0x400> actor{};
    auto predicate=[&](){Vec3 center{};std::memcpy(&center,actor.data()+0x94,12);center.y+=original.centerOffset();
        Vec3 front=center;return invoke(original.predicate(),aev.data(),&center,&front,actor.data())!=0;};
    field(actor.data(),0x94,Vec3{35000,3500,14000});check(predicate(),"original U-3 registered polygon rejected native playable feet");
    field(actor.data(),0x94,Vec3{10000,3500,14000});check(!predicate(),"original polygon accepted feet outside the native trigger");
    field(actor.data(),0x94,Vec3{35000,6000,14000});check(!predicate(),"original polygon ignored native vertical span");
    field(actor.data(),0x94,Vec3{35000,3500,14000});
    for(uint16_t flags:{uint16_t(0x31f),uint16_t(0x1f),uint16_t(0)}){
        field(actor.data(),0x2ce,flags);field(actor.data(),0x110,Vec3{35000,3500,14000});field(actor.data(),0x104,Vec3{});
        check(predicate(),"unchanged old-position/zero speed/collision flags blocked actual position trigger");
    }
    std::array<uint8_t,0x5040> global{};uint64_t keys[2]{};
    field(global.data(),0x20,uint8_t(3));field(global.data(),0x4fac,uint16_t(0x50f));
    field(global.data(),0x4fb4,int16_t(100));field(global.data(),0x4fb6,int16_t(100));
    field(global.data(),0x134,float(70));field(global.data(),0x70,float(1.f/60.f));
    field(actor.data(),0,uint32_t(0x123400));field(actor.data(),4,uint32_t(1));field(actor.data(),0xf8,uint32_t(456));
    uint32_t globalSlot=uint32_t(reinterpret_cast<uintptr_t>(global.data())),actorSlot=uint32_t(reinterpret_cast<uintptr_t>(actor.data()));
    GameAccess game;game.initializeReadFixture(reinterpret_cast<uintptr_t>(&globalSlot),reinterpret_cast<uintptr_t>(&actorSlot));
    game.keyOn_=reinterpret_cast<uintptr_t>(keys);game.keyTrigger_=game.keyOn_+8;
    check(game.sample().canReplace(),"owned native read fixture failed playable witness");
    GuestController guest;guest.game_=&game;guest.installed_=true;guest.setActive(true,7,1000,BridgeMode::Minecraft);
    GuestMotion motion{};motion.epoch=7;motion.tick=1;motion.flags=MotionReady|MotionGrounded;motion.health=motion.maxHealth=20;motion.fov=70;
    motion.hostX=35;motion.hostY=3.5;motion.hostZ=14;
    check(guest.updateMotion(motion),"production controller rejected normal source pose");
    guest.beforeSchedule();check(predicate(),"production F8 scheduler pose failed actual original U-3 polygon");
    guest.setNativeTraversalHold(true);const Vec3 landing{32000,3500,15500};field(actor.data(),0x94,landing);
    guest.beforeSchedule();check(predicate(),"held native landing failed actual original U-3 polygon");
    Vec3 observed{};std::memcpy(&observed,actor.data()+0x94,12);check(observed.x==landing.x&&observed.z==landing.z,"script landing did not remain exact");
    for(unsigned i=0;i<1000;++i){field(actor.data(),0x94,Vec3{35000,3500,14000});check(predicate(),"repeat original inside predicate changed");
        field(actor.data(),0x94,Vec3{10000,3500,14000});check(!predicate(),"repeat original outside predicate changed");}
    std::printf("Native U-3 AEV: %u actual x86 original-byte checks passed; real room0x50f polygon, normal F8 absolute pose, unchanged speed/old/collision semantics and held script landing. No live game/intro/spawn executed.\n",checks);
    return 0;
}catch(const std::exception& error){std::fprintf(stderr,"Original U-3 trigger fixture failed after %u checks: %s\n",checks,error.what());return 1;}}
