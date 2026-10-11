#include "../native/game_access.hpp"
#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
using namespace re4craft;
namespace {
unsigned checks{};
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
template<class T,size_t N>void put(std::array<uint8_t,N>& memory,size_t offset,const T& value){std::memcpy(memory.data()+offset,&value,sizeof(value));}
}
int main(){try{
    std::array<uint8_t,0x5040> global{};std::array<uint8_t,0x104> actor{};
    uint32_t globalSlot=uint32_t(reinterpret_cast<uintptr_t>(global.data()));
    uint32_t actorSlot=uint32_t(reinterpret_cast<uintptr_t>(actor.data()));
    GameAccess game;game.initializeReadFixture(reinterpret_cast<uintptr_t>(&globalSlot),reinterpret_cast<uintptr_t>(&actorSlot));
    put(global,0x20,uint8_t(3));put(global,0x54,uint32_t(0x04000000));
    put(global,0x4f9a,uint8_t(0));put(global,0x4fc8,uint8_t(0));
    put(global,0x4fac,uint16_t(0x100));put(global,0x4fb4,int16_t(100));
    put(actor,0,uint32_t(0x123400));put(actor,4,uint32_t(1));
    put(actor,0xf8,uint32_t(765));put(actor,0x94,Vec3{200,300,400});put(actor,0xa0,Vec3{0,1,0});
    // The exact action witness has no dependency on a valid native camera;
    // the callback's own completion supplies movement ownership instead.
    put(global,0x134,std::numeric_limits<float>::quiet_NaN());
    auto s=game.traversalState();check(s.valid&&s.alive&&s.playing,"minimal live action identity rejected due to missing camera");
    check(s.globalAddress==globalSlot&&s.playerAddress==actorSlot&&s.playerGuid==765&&s.room==0x100,"exact player/global/room/GUID witness changed");
    check(s.position.x==200&&s.position.y==300&&s.position.z==400&&s.angles.y==1,"exact native action position changed");
    auto loot=game.lootContext();check(loot.valid&&loot.chapter==0&&loot.character==0&&loot.systemFlags==0x04000000,"chapter0/system flags context read wrong SDK bytes");
    put(global,0x4f9a,uint8_t(18));put(global,0x4fc8,uint8_t(5));put(global,0x54,uint32_t(0x13579bdf));
    loot=game.lootContext();check(loot.valid&&loot.chapter==18&&loot.character==5&&loot.systemFlags==0x13579bdf,"all loot context fields not freshly sampled");
    put(actor,0xf8,uint32_t(766));check(game.traversalState().playerGuid==766,"reused actor identity retained old GUID");
    put(actor,4,uint32_t(0x201));s=game.traversalState();check(s.valid&&!s.alive,"dead/destroyed native player gained eligibility");
    put(actor,4,uint32_t(1));put(global,0x4fb4,int16_t(0));check(!game.traversalState().alive,"native playerHP0 gained alive witness");
    put(global,0x4fb4,int16_t(100));put(global,0x20,uint8_t(6));check(!game.traversalState().playing,"native menu gained gameplay witness");
    put(global,0x20,uint8_t(3));put(actor,0x94,Vec3{0,std::numeric_limits<float>::quiet_NaN(),0});
    check(!game.traversalState().valid,"non-finite original landing was accepted");
    actorSlot=0;check(!game.traversalState().valid,"missing player slot yielded valid action witness");
    globalSlot=0;check(!game.lootContext().valid&&!game.traversalState().valid,"missing global slot granted loot/traversal authority");
    void* inaccessible=VirtualAlloc(nullptr,0x10000,MEM_COMMIT|MEM_RESERVE,PAGE_NOACCESS);
    check(inaccessible!=nullptr,"unreadable page fixture allocation failed");
    globalSlot=uint32_t(reinterpret_cast<uintptr_t>(inaccessible));
    check(!game.lootContext().valid&&!game.traversalState().valid,"unreadable current global did not fail closed");
    VirtualFree(inaccessible,0,MEM_RELEASE);
    std::printf("Native window/loot access: %u actual x86 memory-read/identity/fail-closed checks passed. No game process touched.\n",checks);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"Window access fixture failed after %u checks: %s\n",checks,error.what());return 1;}}
