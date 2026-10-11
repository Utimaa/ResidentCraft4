#include <cstdint>
#include <cstddef>
#include <mutex>
#include <atomic>
#include <vector>
#include <algorithm>
#include <initializer_list>
#include <array>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <Windows.h>
#undef near
// Fixture-only access wires owned memory to the real controller callbacks;
// it never installs a guessed hook or changes the production class layout.
// Standard headers precede the access macro, so their internals are untouched.
#define RE4CRAFT_ACCESS_FIXTURE
#define private public
#include "guest_controller.hpp"
#undef private
#include "host_controls.hpp"
#include "vehicle_grab_guard.hpp"
using namespace re4craft;
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
bool near(float a,float b){return std::abs(a-b)<1e-4f;}
void nativeTraversalHoldTests(){
    unsigned assertions{};
    auto verify=[&](bool ok,const char* why){++assertions;check(ok,why);};
    std::array<uint8_t,0x5040> global{};std::array<uint8_t,0x400> actor{};uint64_t keys[2]{};
    auto put=[&](auto& memory,size_t at,const auto& value){std::memcpy(memory.data()+at,&value,sizeof(value));};
    auto position=[&](){Vec3 p{};std::memcpy(&p,actor.data()+0x94,sizeof(p));return p;};
    auto oldPosition=[&](){Vec3 p{};std::memcpy(&p,actor.data()+0x110,sizeof(p));return p;};
    auto speed=[&](){Vec3 p{};std::memcpy(&p,actor.data()+0x104,sizeof(p));return p;};
    auto collision=[&](){uint16_t value{};std::memcpy(&value,actor.data()+0x2ce,2);return value;};
    auto same=[](Vec3 a,Vec3 b){return near(a.x,b.x)&&near(a.y,b.y)&&near(a.z,b.z);};
    put(global,0x20,uint8_t(3));put(global,0x4fac,uint16_t(0x100));put(global,0x4fb4,int16_t(100));put(global,0x4fb6,int16_t(100));
    put(global,0x134,float(70));put(global,0x70,float(1.f/60.f));put(global,0x4fc8,uint8_t(0));
    put(actor,0,uint32_t(0x123400));put(actor,4,uint32_t(1));put(actor,0xf8,uint32_t(765));put(actor,0x2ce,uint16_t(0x31f));
    uint32_t globalSlot=uint32_t(reinterpret_cast<uintptr_t>(global.data())),actorSlot=uint32_t(reinterpret_cast<uintptr_t>(actor.data()));
    GameAccess game;game.initializeReadFixture(reinterpret_cast<uintptr_t>(&globalSlot),reinterpret_cast<uintptr_t>(&actorSlot));
    game.keyOn_=reinterpret_cast<uintptr_t>(keys);game.keyTrigger_=game.keyOn_+8;
    verify(game.sample().canReplace(),"owned actor/global read fixture did not grant actual live sample");
    GuestController guest;guest.game_=&game;guest.installed_=true;
    GuestMotion motion{};motion.epoch=7;motion.flags=MotionReady|MotionGrounded;motion.tick=1;motion.health=motion.maxHealth=20;motion.fov=70;
    motion.hostX=3;motion.hostY=2;motion.hostZ=1;
    guest.setActive(true,7,1000,BridgeMode::Minecraft);verify(guest.updateMotion(motion)&&guest.active(),"fresh Minecraft controller did not activate owned memory");
    guest.beforeSchedule();verify(same(position(),{3000,2000,1000})&&same(oldPosition(),position()),"actual scheduler callback baseline did not apply Minecraft pose");
    verify(collision()==0x31f,"scheduler baseline changed original collision flags");
    put(actor,0x94,Vec3{2900,1900,1000});guest.beforeModelMatrix(actor.data());verify(same(position(),{3000,2000,1000}),"actual model callback baseline did not apply Minecraft pose");
    guest.beforePlayerMove(actor.data());verify(collision()==0x1f,"actual player callback baseline did not temporarily relinquish native terrain collision");
    // The native window callback may enter during the original player move.
    // afterPlayerMove must restore a pre-existing borrow, then leave its real
    // coroutine landing untouched while the client/server receipt is pending.
    guest.setNativeTraversalHold(true);
    const Vec3 landing{3200,1500,1050},nativeSpeed{200,-500,50};
    put(actor,0x94,landing);put(actor,0x110,Vec3{3000,2000,1000});put(actor,0x104,nativeSpeed);
    guest.afterPlayerMove(actor.data());verify(collision()==0x31f,"entering traversal inside player move leaked borrowed collision flags");
    verify(same(position(),landing)&&same(oldPosition(),{3000,2000,1000})&&same(speed(),nativeSpeed),"held after-player callback overwrote native window landing or speed");
    guest.beforePlayerMove(actor.data());verify(collision()==0x31f&&same(position(),landing),"held before-player callback disabled native window collision or moved its actor");
    guest.beforeModelMatrix(actor.data());verify(same(position(),landing)&&same(speed(),nativeSpeed),"held model callback overwrote actual native movement fields");
    guest.beforeSchedule();verify(same(position(),landing)&&same(oldPosition(),{3000,2000,1000})&&same(speed(),nativeSpeed),"held scheduler overwrote original coroutine delta");
    verify(!guest.suppressPlayerMove(),"held live guest prevented original native player move");
    motion.tick=2;motion.health=0;motion.flags|=MotionDead;verify(guest.updateMotion(motion),"held dead source receipt did not exercise actual guest state");
    verify(!guest.suppressPlayerMove(),"held dead-source receipt suppressed original window coroutine");
    guest.setActive(false,7,1000,BridgeMode::Minecraft);motion.tick=3;motion.health=20;motion.flags&=~MotionDead;
    guest.setActive(true,7,1000,BridgeMode::Minecraft);verify(guest.updateMotion(motion)&&guest.active(),"reactivated same-epoch source did not exercise retained traversal hold");
    guest.beforeSchedule();verify(same(position(),landing)&&collision()==0x31f,"setActive(false) silently cleared native traversal hold");
    guest.restore();guest.setActive(true,7,1000,BridgeMode::Minecraft);motion.tick=1;
    verify(guest.updateMotion(motion)&&guest.active(),"source did not reactivate after native restore");
    guest.beforeSchedule();verify(same(position(),landing)&&same(speed(),nativeSpeed),"restore silently cleared receipt-owned native traversal hold");
    guest.setNativeTraversalHold(false);motion.tick=2;motion.hostX=4.2;motion.hostY=1.5;motion.hostZ=1.05;
    verify(guest.updateMotion(motion),"fresh acknowledged Minecraft landing receipt was rejected");
    guest.beforeSchedule();verify(same(position(),{4200,1500,1050})&&same(oldPosition(),position())&&same(speed(),{}),"releasing traversal did not resume actual fresh Minecraft scheduler writes");
    verify(collision()==0x31f,"receipt handoff changed original native collision flags");
    put(actor,0x94,Vec3{4100,1500,1050});guest.beforeModelMatrix(actor.data());verify(same(position(),{4200,1500,1050}),"released traversal model callback did not resume guest pose");
    guest.restore();
    std::printf("PASS: %u actual x86 controller callback/memory traversal-hold checks; scheduler/model writes, held native landing/speed/collision, original-player eligibility, retained hold and fresh receipt release. No native window coroutine or game process executed.\n",assertions);
}
void vehicleGrabScopeTests(){
    constexpr uintptr_t player=0x120000;
    std::array<uint8_t,0x400> bytes{};
    uint32_t identity=17;std::memcpy(bytes.data()+0xf8,&identity,4);
    unsigned reads{},writes{};bool failRead=false,failWrite=false;
    auto read=[&](uintptr_t at,void* out,size_t size){++reads;if(failRead||at<player||at-player+size>bytes.size())return false;std::memcpy(out,bytes.data()+at-player,size);return true;};
    auto write=[&](uintptr_t at,const void* in,size_t size){++writes;if(failWrite||at<player||at-player+size>bytes.size())return false;check(at==player+0x32d&&size==1,"grab scope changed another native field");std::memcpy(bytes.data()+at-player,in,size);return true;};
    ScopedVehicleGrabProtection scope;
    bytes[0x32d]=57;
    check(!scope.begin(false,player,read,write)&&bytes[0x32d]==57&&reads==0&&writes==0,"native/Minecraft/scripted scope touched catch timer");
    check(!scope.end(player,read,write)&&writes==0,"ineligible scope wrote on exit");
    check(scope.begin(true,player,read,write)&&bytes[0x32d]==128,"fresh vehicle did not reject native catch before scheduler");
    bytes[0x32d]=127; // Original scheduler may decrement its temporary timer.
    check(scope.end(player,read,write)&&bytes[0x32d]==57,"scheduler scope leaked invulnerability or lost original timer");
    check(!scope.end(player,read,write),"duplicate scope exit wrote stale original timer");
    check(scope.begin(true,player,read,write),"nested guard outer entry failed");
    check(scope.begin(true,player,read,write),"nested guard inner entry failed");
    check(!scope.end(player,read,write)&&bytes[0x32d]==128,"nested exit released original scheduler guard");
    check(scope.end(player,read,write)&&bytes[0x32d]==57,"outer exit did not restore timer");
    check(scope.begin(true,player,read,write),"replacement check entry failed");
    const auto beforeReplacement=writes;
    check(!scope.end(player+0x1000,read,write)&&writes==beforeReplacement,"changed native player allocation restored stale bytes");
    bytes[0x32d]=0;
    check(scope.begin(true,player,read,write),"identity check entry failed");
    identity=18;std::memcpy(bytes.data()+0xf8,&identity,4);
    const auto beforeIdentity=writes;
    check(!scope.end(player,read,write)&&writes==beforeIdentity,"reused native allocation restored another actor timer");
    bytes[0x32d]=0;failRead=true;
    check(!scope.begin(true,player,read,write)&&bytes[0x32d]==0,"unreadable actor accepted grab ownership");scope.end(player,read,write);failRead=false;
    failWrite=true;check(!scope.begin(true,player,read,write)&&bytes[0x32d]==0,"failed guard write claimed timer");scope.end(player,read,write);failWrite=false;
    const auto beforeAddress=writes;
    check(!scope.begin(true,UINT32_MAX,read,write),"overflowing x86 actor accepted grab ownership");scope.end(player,read,write);
    check(writes==beforeAddress,"invalid player address wrote catch timer");
}
int main(){try{
    nativeTraversalHoldTests();
    vehicleGrabScopeTests();
    for(unsigned c=0;c<=18;++c)check(nativeLootTier(uint8_t(c),0,0)==(c<=2?0:c<=5?1:c<=9?2:c<=13?3:c<=17?4:5),"native zero-based subchapter loot tier mismatch");
    for(auto c:{19,20,21,25,255})check(nativeLootTier(uint8_t(c),0,0)==-1,"unsupported chapter converted native loot");
    check(nativeLootTier(7,2,0)==-1,"SeparateWays character converted maincampaign loot");
    for(auto bonus:{0x80000000U,0x40000000U,0x1000U})check(nativeLootTier(0,0,bonus)==-1,"native bonus-game loot was converted using main-campaign progression");
    check(nativeLootItemEligible(4,10,0,0)&&nativeLootItemEligible(120,100,0,0),"ordinary ammo/money not eligible");
    for(auto id:{12,15,27,29,35,59,61,119,122,141,272,4096})check(!nativeLootItemEligible(id,1,0,0),"quest/weapon/treasure/special loot converted");
    check(!nativeLootItemEligible(4,0,0,0)&&!nativeLootItemEligible(4,65536,0,0)&&!nativeLootItemEligible(4,1,1,0)&&!nativeLootItemEligible(4,1,0,1),"scripted or invalid item accepted");
    NativeLootQueue loot;HostLoot accepted{};check(!loot.append(1,1,0,0,0,0,accepted),"uninitialized loot queue suppressed native item");loot.initialize(123);
    for(unsigned i=0;i<128;++i)check(loot.append(i<64?1:2,1,0,0,0,0,accepted)&&accepted.eventId==i+1&&accepted.sessionId==123,"loot identity reset at room epoch");
    check(!loot.append(2,2,4,0,0,0,accepted),"full loot queue suppressed native fallback");auto events=loot.take();check(events.size()==128,"loot take lost event");
    check(loot.append(2,2,4,1,2,3,accepted)&&accepted.eventId==129,"draining loot reset monotonic event identity");
    check(loot.retry(events[1])&&loot.retry(events[0])&&loot.retry(events[1]),"loot retry failed");auto replay=loot.take();check(replay.size()==3&&replay[0].eventId==1&&replay[1].eventId==2&&replay[2].eventId==129,"loot retry duplicated/reordered IDs");
    auto foreign=events[0];foreign.sessionId=456;check(!loot.retry(foreign),"foreign loot session accepted");loot.initialize(456);check(loot.append(3,1,1,0,0,0,accepted)&&accepted.sessionId==123&&accepted.eventId==130,"reinitializing changed plugin loot identity");
    GuestMotion motion{};motion.epoch=7;motion.flags=MotionReady|MotionGrounded;motion.tick=1;motion.health=20;motion.maxHealth=20;motion.fov=70;
    check(validGuestMotion(motion,7),"valid motion rejected");check(!validGuestMotion(motion,8),"wrong-room motion accepted");
    motion.hostX=std::numeric_limits<double>::quiet_NaN();check(!validGuestMotion(motion,7),"NaN position accepted");motion.hostX=0;
    motion.velocityY=17;check(!validGuestMotion(motion,7),"unbounded velocity accepted");motion.velocityY=0;
    motion.velocityX=30;check(!validGuestMotion(motion,7)&&validGuestMotion(motion,7,true),"vehicle velocity changed Minecraft bounds");motion.velocityX=129;check(!validGuestMotion(motion,7,true),"explicit vehicle motion exceeded cap");motion.velocityX=0;
    GuestController isolated;isolated.setActive(false,7,1000,true);check(!isolated.updateMotion(motion),"vehicle pose accepted missing real camera");motion.flags|=MotionCameraReady;check(isolated.updateMotion(motion),"explicit vehicle pose rejected");check(!isolated.updateMotion(motion),"replayed vehicle pose accepted");isolated.setActive(false,7,1000,false);motion.tick=2;motion.velocityX=30;check(!isolated.updateMotion(motion),"same-epoch vehicle-to-Minecraft switch retained broad velocity bounds");motion.velocityX=0;check(isolated.updateMotion(motion),"same-epoch vehicle mode switch retained prior motion sequence");
    motion.cameraMode=3;check(!validGuestMotion(motion,7),"unknown camera mode accepted");motion.cameraMode=0;
    GuestController modes;
    modes.setActive(false,7,1000,BridgeMode::Minecraft);
    check(modes.setBlocks(7,{{0,0,0,1,1,1}}),"actual Minecraft mode rejected placed colliders");
    for(auto mode:{BridgeMode::Halo,BridgeMode::Ultrakill,BridgeMode::BeamNG,BridgeMode::Native,static_cast<BridgeMode>(99)}){
        modes.setActive(false,7,1000,mode);
        check(!modes.setBlocks(7,{{0,0,0,1,1,1}}),"non-Minecraft mode accepted placed-block collision policy");
        check(!modes.interceptLoot(1,1,nullptr,1,1),"non-Minecraft controller intercepted native loot");
        check(!modes.retryLoot({}),"non-Minecraft controller accepted a Minecraft loot retry");
        check(modes.takeLoot().empty(),"Minecraft loot was delivered through another source controller");
        check(!modes.constrainEnemyAtari(reinterpret_cast<void*>(1)),"non-Minecraft mode ran placed-block scene adjustment");
    }
    modes.setActive(false,7,1000,BridgeMode::Halo);motion.flags=MotionReady;
    check(!modes.updateMotion(motion),"Halo movement accepted without authentic camera");
    motion.flags|=MotionCameraReady;motion.tick=10;
    check(modes.updateMotion(motion),"valid bounded Halo motion receipt rejected");
    check(!modes.updateMotion(motion),"Halo accepted a replayed motion receipt");
    modes.setActive(false,7,1000,BridgeMode::Minecraft);motion.tick=2;
    check(modes.updateMotion(motion),"same-epoch Halo-to-Minecraft handoff retained another source sequence");
    check(modes.setBlocks(7,{{0,0,0,1,1,1}}),"returning to Minecraft lost block collider ownership");
    modes.setActive(false,7,1000,BridgeMode::Ultrakill);
    check(!modes.updateMotion(motion)&&!validGuestMotion(motion,7,BridgeMode::Ultrakill),"unverified ULTRAKILL bounds granted physics");
    check(!validGuestMotion(motion,7,BridgeMode::Native)&&!validGuestMotion(motion,7,static_cast<BridgeMode>(99)),"native/unknown mode accepted guest movement");
    modes.restore();check(!modes.setBlocks(0,{}),"native restoration retained Minecraft block ownership");
    motion.health=0;motion.flags|=MotionDead;check(validGuestMotion(motion,7),"dead guest motion rejected");
    std::vector<BlockBox> boxes{{1,0,-2,2,3,2}};check(validBlockBox(boxes[0]),"valid block box rejected");
    BlockBox invalid{2,0,0,1,1,1};check(!validBlockBox(invalid),"inverted collider accepted");
    auto p=clipEnemyMotion({0,0,0},{3,0,0},.3f,1.8f,boxes);check(near(p.x,.7f),"fast enemy crossed placed wall");
    p=clipEnemyMotion({3,0,0},{0,0,0},.3f,1.8f,boxes);check(near(p.x,2.3f),"reverse wall sweep failed");
    p=clipEnemyMotion({0,4,0},{3,4,0},.3f,1.8f,boxes);check(near(p.x,3),"enemy above wall blocked");
    p=clipEnemyMotion({0,0,0},{.4f,0,1},.3f,1.8f,boxes);check(near(p.x,.4f)&&near(p.z,1),"unobstructed movement changed");
    p=clipEnemyMotion({1.1f,0,0},{1.1f,0,0},.3f,1.8f,boxes);check(near(p.x,.7f),"new wall overlapping native actor remained penetrated");
    p=clipEnemyMotion({1.9f,0,0},{1.9f,0,0},.3f,1.8f,boxes);check(near(p.x,2.3f),"native actor overlapping far wall side crossed to wrong side");
    p=clipEnemyMotion({.7f,0,0},{.7f,0,1},.3f,1.8f,boxes);check(near(p.x,.7f)&&near(p.z,1),"wall contact prevented tangential native AI movement");
    boxes={{-2,0,-2,2,1,2}};p=clipEnemyMotion({0,3,0},{0,-3,0},.3f,1.8f,boxes);check(near(p.y,1),"enemy fell through block floor");
    p=clipEnemyMotion({0,-3,0},{0,3,0},.3f,1.8f,boxes);check(near(p.y,-1.8f),"enemy passed through block ceiling");
    boxes={{1,0,-2,2,3,2},{.8f,0,-2,1.8f,3,2}};p=clipEnemyMotion({0,0,0},{3,0,0},.3f,1.8f,boxes);check(near(p.x,.5f),"nearest of overlapping walls not chosen");
    boxes={{1,0,-2,2,3,2},{2,0,-2,3,3,2}};p=clipEnemyMotion({1.95f,0,0},{1.95f,0,0},.3f,1.8f,boxes);check(near(p.x,.7f),"adjacent-cell overlap oscillated instead of clearing union");
    check(blockedByGuestBlocks({0,1,0},{3,1,0},boxes)&&blockedByGuestBlocks({3,1,0},{0,1,0},boxes),"melee passed through placed block");
    check(!blockedByGuestBlocks({0,4,0},{3,4,0},boxes)&&!blockedByGuestBlocks({0,1,3},{3,1,3},boxes),"unblocked attack rejected");
    Enemy enemy{};enemy.id=3;enemy.flags=1;enemy.x=2;enemy.minX=-.3f;enemy.maxX=.3f;enemy.minY=0;enemy.maxY=1.8f;enemy.minZ=-.3f;enemy.maxZ=.3f;
    GuestMelee hit{};hit.epoch=7;hit.targetId=3;hit.eventId=1;hit.damage=4;hit.maxReach=3;hit.attackY=1.62;hit.directionX=1;
    check(meleeInReach(hit,enemy,{0,0,0}),"in-range melee rejected");enemy.x=8;check(!meleeInReach(hit,enemy,{0,0,0}),"out-of-range enemy accepted");enemy.x=2;
    hit.flags=1;check(meleeInReach(hit,enemy,{0,0,0}),"vanilla critical attack rejected");hit.flags=64;check(!meleeInReach(hit,enemy,{0,0,0}),"unknown attack flag accepted");hit.flags=0;
    hit.attackX=10;check(!meleeInReach(hit,enemy,{0,0,0}),"attack from remote origin accepted");hit.attackX=0;
    hit.flags=2;hit.maxReach=1;hit.attackX=2;check(meleeInReach(hit,enemy,{0,0,0}),"verified in-range projectile impact rejected");
    hit.attackX=4;check(!meleeInReach(hit,enemy,{0,0,0}),"projectile remote from target accepted");
    hit.attackX=2;hit.maxReach=3;check(!meleeInReach(hit,enemy,{0,0,0}),"projectile extended reach accepted");
    hit.maxReach=1;enemy.x=130;hit.attackX=130;check(!meleeInReach(hit,enemy,{0,0,0}),"projectile beyond native 128m actor radius accepted");
    enemy.x=2;hit.attackX=0;hit.flags=0;hit.maxReach=3;
    hit.flags=12;hit.maxReach=1;hit.attackX=2;check(meleeInReach(hit,enemy,{0,0,0}),"actual lava/fire contact rejected");
    hit.maxReach=1.01f;check(!meleeInReach(hit,enemy,{0,0,0}),"hazard gained extended contact range");hit.maxReach=1;
    hit.flags=9;check(!meleeInReach(hit,enemy,{0,0,0}),"hazard combined with critical accepted");hit.flags=10;check(!meleeInReach(hit,enemy,{0,0,0}),"hazard combined with projectile accepted");hit.flags=24;check(!meleeInReach(hit,enemy,{0,0,0}),"hazard/explosion ambiguity accepted");
    hit.flags=16;hit.attackX=-6;hit.maxReach=8;check(meleeInReach(hit,enemy,{0,0,0}),"actual TNT exposed damage rejected");hit.maxReach=16;hit.attackX=-14;check(meleeInReach(hit,enemy,{0,0,0}),"bounded larger native explosion rejected");
    hit.maxReach=16.01f;check(!meleeInReach(hit,enemy,{0,0,0}),"oversized blast radius accepted");hit.maxReach=8;hit.flags=18;check(!meleeInReach(hit,enemy,{0,0,0}),"projectile/explosion ambiguity accepted");
    hit.flags=16;hit.attackX=130;enemy.x=130;check(!meleeInReach(hit,enemy,{0,0,0}),"environment source beyond actor radius accepted");
    check(!guestAttackUsesBlockRay(16)&&!guestAttackUsesBlockRay(12)&&guestAttackUsesBlockRay(0)&&guestAttackUsesBlockRay(2),"vanilla partial blast exposure overridden by guest straight-ray test");
    enemy.x=2;hit.attackX=0;hit.flags=0;hit.maxReach=3;
    {
        HostDamage incoming{};incoming.kind=1;
        check(attachHostDamagePosition(incoming,{10000,1620,5000},1000,{9,0,5}),"verified native attack origin rejected");
        check((incoming.flags&DamageHasPosition)&&near(float(incoming.sourceX),10)&&near(float(incoming.sourceY),1.62f),"native shield source used wrong scale");
        HostDamage fatal{};fatal.kind=1;fatal.flags=DamageFatal;
        check(!attachHostDamagePosition(fatal,{1000,0,0},1000,{0,0,0})&&fatal.flags==DamageFatal,"fatal native event became shieldable");
        HostDamage anonymous{};anonymous.kind=0;
        check(!attachHostDamagePosition(anonymous,{1000,0,0},1000,{0,0,0})&&!anonymous.flags,"anonymous environmental event invented direction");
        incoming={};incoming.kind=1;
        check(!attachHostDamagePosition(incoming,{129000,0,0},1000,{0,0,0}),"remote native damage source accepted");
        check(!attachHostDamagePosition(incoming,{0,0,0},0,{0,0,0}),"invalid source scale accepted");
        check(!attachHostDamagePosition(incoming,{std::numeric_limits<float>::quiet_NaN(),0,0},1000,{0,0,0}),"NaN native damage source accepted");
        check(!(incoming.flags&DamageHasPosition),"invalid origin retained source flag");
        GuestMelee ally{};ally.targetId=3;ally.eventId=1;ally.damage=4;ally.flags=HitAlly;ally.maxReach=3;ally.attackX=80;ally.attackY=.6;ally.directionX=1;
        Enemy target=enemy;target.x=82;target.flags=ActorAlive|ActorHostile;
        check(meleeInReach(ally,target,{0,0,0}),"owned wolf attack away from player rejected");
        target.flags=ActorAlive;check(!meleeInReach(ally,target,{0,0,0}),"wolf attacked unverified/friendly native actor");target.flags=ActorAlive|ActorHostile;
        ally.flags|=HitProjectile;check(!meleeInReach(ally,target,{0,0,0}),"allied hit combined with projectile accepted");ally.flags=HitAlly;
        ally.maxReach=3.01f;check(!meleeInReach(ally,target,{0,0,0}),"wolf attack extended reach accepted");ally.maxReach=3;
        target.flags|=ActorBreakable;check(!meleeInReach(ally,target,{0,0,0}),"wolf attacked native crate");target.flags=ActorAlive|ActorHostile;
        ally.attackX=129;target.x=130;check(!meleeInReach(ally,target,{0,0,0}),"allied source exceeded128m");
        check(guestAttackUsesBlockRay(HitAlly)&&!nativeBreakableAttack(HitAlly),"wolf wall/crate defense disabled");
    }
    hit.damage=std::numeric_limits<float>::quiet_NaN();check(!meleeInReach(hit,enemy,{0,0,0}),"NaN melee damage accepted");
    check(!nativeEnemyKind(0)&&!nativeEnemyKind(0x18)&&!nativeEnemyKind(0x2a)&&!nativeEnemyKind(0x3b)&&nativeEnemyKind(0x11),"native actors/traps classified as melee enemies");
    check(nativeBreakableKind(0x53)&&!nativeBreakableKind(0x58)&&!nativeEnemyKind(0x53),"crate scope included scripted barrel or ordinary enemy");
    check(nativeBreakableAttack(0)&&nativeBreakableAttack(1)&&nativeBreakableAttack(2)&&nativeBreakableAttack(16)&&!nativeBreakableAttack(12)&&!nativeBreakableAttack(4),"crate route accepted unsupported fire/hazard damage");
    enemy.health=10;enemy.maxHealth=20;enemy.kind=0x11;std::vector<Enemy> snapshot;
    check(appendUniqueEnemy(snapshot,enemy)&&snapshot.size()==1,"valid native actor filtered from snapshot");
    check(!appendUniqueEnemy(snapshot,enemy)&&snapshot.size()==1,"duplicate GUID would disconnect guest decoder");
    auto invalidEnemy=enemy;invalidEnemy.id=4;invalidEnemy.maxY=129;check(!appendUniqueEnemy(snapshot,invalidEnemy),"oversized native atari bounds published");
    invalidEnemy=enemy;invalidEnemy.id=4;invalidEnemy.minX=std::numeric_limits<float>::quiet_NaN();check(!appendUniqueEnemy(snapshot,invalidEnemy),"nonfinite native atari bounds published");
    invalidEnemy=enemy;invalidEnemy.id=4;invalidEnemy.health=21;check(!appendUniqueEnemy(snapshot,invalidEnemy),"native HP above maximum published");
    invalidEnemy=enemy;invalidEnemy.id=0x80000000;check(!appendUniqueEnemy(snapshot,invalidEnemy),"unsigned enemy ID would become negative at guest");
    check(nativeEnemyRefreshDue(0,1)&&!nativeEnemyRefreshDue(100,149)&&nativeEnemyRefreshDue(100,150)&&!nativeEnemyRefreshDue(100,90),"enemy snapshot50ms cache boundary failed");
    check(nativeHitPartAcceptsWeapon(1,true)&&nativeHitPartAcceptsWeapon(0x11,false)&&!nativeHitPartAcceptsWeapon(0x11,true)&&!nativeHitPartAcceptsWeapon(0,false),"direct native reaction bypassed disabled/protected body-part filter");
    float identity[3][4]{{1,0,0,10},{0,1,0,20},{0,0,1,30}};NativeHitShape capsule{};capsule.flags=1;capsule.offset={0,100,0};capsule.radius=200;capsule.height=1200;NativeActorBounds bounds{};
    check(appendNativeHitShapeBounds(bounds,capsule,identity,{10,20,30})&&near(bounds.minimum.x,-200)&&near(bounds.minimum.y,-100)&&near(bounds.maximum.y,1500),"native attack capsule confused with movement capsule or actor translation");
    capsule.flags=3;bounds={};check(appendNativeHitShapeBounds(bounds,capsule,identity,{10,20,30})&&near(bounds.maximum.x,1400)&&near(bounds.maximum.y,300),"native capsule X-axis flag ignored");
    capsule.flags=5;bounds={};check(appendNativeHitShapeBounds(bounds,capsule,identity,{10,20,30})&&near(bounds.maximum.z,1400),"native capsule Z-axis flag ignored");
    float rotated[3][4]{{0,-1,0,1010},{1,0,0,2020},{0,0,2,3030}};capsule.flags=1;bounds={};check(appendNativeHitShapeBounds(bounds,capsule,rotated,{10,20,30})&&near(bounds.minimum.x,-700)&&near(bounds.maximum.x,1300)&&near(bounds.minimum.y,1600)&&near(bounds.maximum.z,3400),"animated part matrix/radius native Z-column scale ignored");
    NativeHitShape rectangle{};rectangle.flags=9;rectangle.radius=300;rectangle.height=1800;rectangle.extent=100;bounds={};check(appendNativeHitShapeBounds(bounds,rectangle,identity,{10,20,30})&&near(bounds.minimum.x,-300)&&near(bounds.minimum.y,0)&&near(bounds.maximum.y,1800)&&near(bounds.maximum.z,100),"native rectangle full versus half extents incorrect");
    capsule.flags=1;capsule.offset={700,100,0};check(appendNativeHitShapeBounds(bounds,capsule,identity,{10,20,30})&&near(bounds.maximum.x,900)&&near(bounds.maximum.y,1800),"linked body-part shape union lost prior bounds");
    auto saved=bounds;capsule.flags=0;check(!appendNativeHitShapeBounds(bounds,capsule,identity,{10,20,30})&&near(bounds.maximum.x,saved.maximum.x),"disabled native shape changed enemy bounds");capsule.flags=1;capsule.radius=std::numeric_limits<float>::quiet_NaN();check(!appendNativeHitShapeBounds(bounds,capsule,identity,{10,20,30}),"nonfinite hit shape changed bounds");
    check((filterGuestMovement(UINT64_MAX,true)&HostMovementButtons)==0&&filterGuestMovement(0x400ULL,true)==0x400ULL,"guest movement filter blocked native action or retained movement");
    CombatDamageLedger ledger;NativeDamageIdentity info{16,0x2000,{1,2,3}};int damage{};
    check(ledger.arm(7,3,1,0x1000,info,200,100),"valid native reaction ledger rejected");
    check(ledger.consume(8,3,0x1000,info,110,damage)==DamageClaim::Unrelated,"old-epoch damage replaced native hit");
    check(ledger.consume(7,4,0x1000,info,110,damage)==DamageClaim::Unrelated,"other actor damage replaced native hit");
    check(ledger.consume(7,3,0x1001,info,110,damage)==DamageClaim::Unrelated,"recycled model damage replaced native hit");
    auto other=info;other.yarare++;check(ledger.consume(7,3,0x1000,other,110,damage)==DamageClaim::Unrelated,"different body-part hit captured");
    other=info;other.source.x++;check(ledger.consume(7,3,0x1000,other,110,damage)==DamageClaim::Unrelated,"unrelated same-target weapon hit captured");
    other=info;other.weapon=2;check(ledger.consume(7,3,0x1000,other,110,damage)==DamageClaim::Unrelated,"native firearm hit captured");
    check(ledger.consume(7,3,0x1000,info,110,damage)==DamageClaim::Apply&&damage==200,"Minecraft damage did not replace knife damage once");
    check(ledger.consume(7,3,0x1000,info,120,damage)==DamageClaim::AlreadyApplied,"repeated native damage double-applied Minecraft hit");
    check(ledger.arm(7,3,2,0x1000,info,350,130),"later Minecraft hit could not replace retired ledger");
    check(!ledger.arm(7,3,3,0x1000,info,400,140),"pending native reaction was overwritten");
    check(ledger.consume(7,3,0x1000,info,431,damage)==DamageClaim::Unrelated,"expired ledger changed a late native hit");
    for(uint32_t id=1;id<=64;++id)check(ledger.arm(7,id,id,0x1000+id,info,100,500),"bounded ledger rejected valid capacity");
    check(!ledger.arm(7,65,65,0x1065,info,100,500),"reaction ledger exceeded64 pending hits");
    ledger.clear();check(ledger.consume(7,1,0x1001,info,510,damage)==DamageClaim::Unrelated,"disabled bridge retained pending damage ownership");
    for(uint32_t weapon:{0x12U,0x13U}){info.weapon=weapon;check(ledger.arm(7,3,1,0x1000,info,400,600),"verified native environment reaction rejected");other=info;other.weapon=0x10;check(ledger.consume(7,3,0x1000,other,601,damage)==DamageClaim::Unrelated,"environment ledger captured different native weapon");check(ledger.consume(7,3,0x1000,info,601,damage)==DamageClaim::Apply&&damage==400,"environment damage not applied once");check(ledger.consume(7,3,0x1000,info,602,damage)==DamageClaim::AlreadyApplied,"environment damage repeated");ledger.clear();}
    check(guestNativeDamageWeaponForFlags(0)==0x10&&guestNativeDamageWeaponForFlags(12)==0x13&&guestNativeDamageWeaponForFlags(16)==0x12&&!guestNativeDamageWeapon(2)&&!guestNativeDamageWeapon(0x17),"native zero-damage ignition substituted for damaging fire");
    std::puts("PASS: guest motion/death bounds, enemy wall/floor sweeps, animated native hit shapes, melee/projectile/environment validation, one-time native reactions, native loot tier/protected-items/ordered queue, actor and input ownership.");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"Guest fixture failed: %s\n",e.what());return 1;}}
