#include "beamng_mode.hpp"
#include "vehicle_interaction.hpp"
#include <cstdio>
#include <limits>
#include <stdexcept>
using namespace re4craft;
static void check(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
static BeamngReadiness ready(){
    BeamngReadiness r{};r.connected=r.hostCanReplace=r.focused=r.rendererReady=r.meshPresent=r.importedTerrain=true;
    r.stateFlags=InWorld;r.meshEpoch=7;r.vehicleExtent=2.5f;r.terrain={7,2,1,0,0,0,0,256};
    r.motion.epoch=7;r.motion.flags=MotionReady|MotionCameraReady;r.motion.health=r.motion.maxHealth=1;r.motion.fov=70;
    return r;
}
int main(){try{
    static_assert(uint8_t(BridgeMode::Native)==0&&uint8_t(BridgeMode::Minecraft)==1&&uint8_t(BridgeMode::BeamNG)==2&&uint8_t(BridgeMode::Halo)==3&&uint8_t(BridgeMode::Ultrakill)==4);
    const BridgeMode modes[]{BridgeMode::Native,BridgeMode::Minecraft,BridgeMode::BeamNG,BridgeMode::Halo,BridgeMode::Ultrakill};
    for(auto current:modes)for(unsigned edges=0;edges<16;++edges){
        const bool f4=edges&1,f6=edges&2,f7=edges&4,f8=edges&8;
        const unsigned count=unsigned(f4)+unsigned(f6)+unsigned(f7)+unsigned(f8);
        const auto requested=f4?BridgeMode::Ultrakill:f6?BridgeMode::Halo:f7?BridgeMode::BeamNG:BridgeMode::Minecraft;
        const auto expected=count!=1?current:current==requested?BridgeMode::Native:requested;
        check(selectedBridgeMode(current,f4,f6,f7,f8)==expected,"guest hotkeys lost current choice, handoff or simultaneous-edge safety");
    }
    check(selectedBridgeMode(static_cast<BridgeMode>(99),true,false,false,false)==BridgeMode::Ultrakill,"new valid edge did not recover an unknown mode");
    for(size_t i=0;i<GuestBridgeModes.size();++i){check(guestBackendIndex(GuestBridgeModes[i])==i,"backend index aliases a different source cache");check(guestBridgeMode(GuestBridgeModes[i]),"valid source peer rejected");}
    check(guestBackendIndex(BridgeMode::Native)==GuestBackendCount&&guestBackendIndex(static_cast<BridgeMode>(99))==GuestBackendCount,"native/unknown mode gained a guest cache slot");
    check(guestMotionVelocityLimit(BridgeMode::Minecraft)==16&&guestMotionVelocityLimit(BridgeMode::Halo)==16&&guestMotionVelocityLimit(BridgeMode::BeamNG)==128&&guestMotionVelocityLimit(BridgeMode::Ultrakill)==0,"unverified source changed established movement bounds");
    SourceGuestReadiness halo{};halo.mode=BridgeMode::Halo;
    halo.connected=halo.hostCanReplace=halo.focused=halo.rendererReady=halo.meshPresent=halo.completeTerrain=true;
    halo.stateFlags=InWorld;halo.meshEpoch=7;halo.terrain={7,2,1,0,0,0,0,32};
    halo.motion.epoch=7;halo.motion.flags=MotionReady|MotionCameraReady;halo.motion.health=halo.motion.maxHealth=20;halo.motion.fov=70;halo.motion.cameraY=1.6;
    check(sourceGuestReady(halo,7),"fully constructed, scoped Halo collision/camera fixture rejected");
    check(!sourceGuestReady({},7),"missing authentic source bootstrapped guest presentation");
    for(unsigned missing=0;missing<6;++missing){auto r=halo;bool* fields[]{&r.connected,&r.hostCanReplace,&r.focused,&r.rendererReady,&r.meshPresent,&r.completeTerrain};*fields[missing]=false;check(!sourceGuestReady(r,7),"missing source readiness gate granted Halo replacement");}
    for(unsigned age=0;age<4;++age){auto r=halo;uint64_t* fields[]{&r.stateAgeMs,&r.meshAgeMs,&r.motionAgeMs,&r.terrainAgeMs};const uint64_t limits[]{750,1000,250,2000};*fields[age]=limits[age];check(!sourceGuestReady(r,7),"expired source/collider data retained Halo authority");*fields[age]=limits[age]-1;check(sourceGuestReady(r,7),"fresh source boundary rejected");}
    for(uint32_t flag:{uint32_t(MotionReady),uint32_t(MotionCameraReady),uint32_t(MotionReady|MotionCameraReady|MotionDead),uint32_t(MotionReady|MotionCameraReady|MotionScreen),uint32_t(MotionReady|MotionCameraReady|32)}){auto r=halo;r.motion.flags=flag;check(!sourceGuestReady(r,7),"missing camera/collider ACK or paused/dead source gained Halo authority");}
    for(uint32_t flag:{uint32_t(0),uint32_t(InWorld|Dead),uint32_t(InWorld|ScreenOpen)}){auto r=halo;r.stateFlags=flag;check(!sourceGuestReady(r,7),"outside-world/dead/screen source gained physics");}
    for(auto mode:{BridgeMode::Native,BridgeMode::Minecraft,BridgeMode::BeamNG,BridgeMode::Ultrakill,static_cast<BridgeMode>(99)}){auto r=halo;r.mode=mode;check(!sourceGuestReady(r,7),"another/unverified backend reused Halo's ready fixture");}
    for(unsigned mismatch=0;mismatch<3;++mismatch){auto r=halo;if(mismatch==0)r.meshEpoch=8;if(mismatch==1)r.motion.epoch=8;if(mismatch==2)r.terrain.epoch=8;check(!sourceGuestReady(r,7),"cross-epoch source/collider retained authority");}
    for(uint32_t flags:{0U,2U,3U}){auto r=halo;r.terrain.flags=flags;check(!sourceGuestReady(r,7),"truncated/incomplete collider receipt gained authority");}
    auto distant=halo;distant.motion.hostX=25;check(!sourceGuestReady(distant,7),"source actor passed beyond complete collision margin");
    distant=halo;distant.motion.cameraX=25;check(!sourceGuestReady(distant,7),"camera passed beyond complete collision margin");
    distant=halo;distant.motion.cameraY=0;distant.motion.hostX=distant.motion.cameraX=24;check(sourceGuestReady(distant,7),"exact bounded collision coverage edge rejected");
    distant.motion.hostX=24.01;check(!sourceGuestReady(distant,7),"source foot escaped bounded collision coverage");
    auto unsafe=halo;unsafe.motion.velocityY=17;check(!sourceGuestReady(unsafe,7),"Halo silently widened legacy movement limit");unsafe=halo;unsafe.motion.fov=std::numeric_limits<float>::quiet_NaN();check(!sourceGuestReady(unsafe,7),"nonfinite authentic camera state gained authority");
    for(auto mode:modes)check(minecraftGameplayRoutes(mode)==(mode==BridgeMode::Minecraft),"Minecraft loot/blocks leaked into another guest");
    check(beamngUiMatches(true,499,true,false,false),"fresh source HUD rejected");
    check(beamngUiMatches(true,499,true,true,true),"fresh original picker rejected");
    check(!beamngUiMatches(true,1,true,true,false),"lingering picker became a driving overlay");
    check(!beamngUiMatches(true,1,true,false,true),"driving HUD gained selector input authority");
    check(!beamngUiMatches(true,500,true,false,false),"expired UI capture accepted");
    check(!beamngUiMatches(true,1,false,false,false)&&!beamngUiMatches(false,1,true,false,false),"unknown or invalid capture gained authority");
    for(auto mode:{BridgeMode::Native,BridgeMode::Minecraft,BridgeMode::BeamNG}){
        check(selectedBridgeMode(mode,false,false)==mode,"no edge changed mode");
        check(selectedBridgeMode(mode,true,true)==mode,"simultaneous keys changed authority by key order");
        check(selectedBridgeMode(selectedBridgeMode(mode,true,false),true,false)==(mode==BridgeMode::Minecraft?BridgeMode::Native:mode),"F7 toggle sequence failed");
        check(selectedBridgeMode(selectedBridgeMode(mode,false,true),false,true)==(mode==BridgeMode::BeamNG?BridgeMode::Native:mode),"F8 toggle sequence failed");
    }
    check(selectedBridgeMode(BridgeMode::Minecraft,true,false)==BridgeMode::BeamNG,"F7 did not hand off Minecraft authority");
    check(selectedBridgeMode(BridgeMode::BeamNG,false,true)==BridgeMode::Minecraft,"F8 did not restore Minecraft choice");
    check(selectedBridgeMode(static_cast<BridgeMode>(99),false,false)==BridgeMode::Native,"invalid mode retained authority");
    check(minecraftGameplayRoutes(BridgeMode::Minecraft)&&!minecraftGameplayRoutes(BridgeMode::BeamNG),"Minecraft gameplay routed to vehicle peer");
    Input held{};held.flags=Enabled|Focused|GuestAuthority;held.held=Forward|Attack;held.pressed=Inventory|Camera;held.scroll=120;held.mouseButtons=3;held.selectedSlot=2;held.hostEpoch=7;held.hostX=42;
    auto disabled=inactiveBridgeInput(held);check(!disabled.flags&&!disabled.held&&!disabled.pressed&&!disabled.scroll&&!disabled.mouseButtons&&disabled.selectedSlot==UINT32_MAX&&disabled.hostEpoch==7&&disabled.hostX==42,"inactive input retained action or lost handshake metadata");
    auto r=ready();check(beamngReady(r,7),"valid imported real-pose readiness rejected");
    auto fail=[&](BeamngReadiness bad,const char* why){check(!beamngReady(bad,7),why);};
    auto bad=r;bad.motion.epoch=6;fail(bad,"old-session pose accepted");bad=r;bad.meshEpoch=6;fail(bad,"cached old-session geometry accepted");
    bad=r;bad.terrain.epoch=6;fail(bad,"old-room collision accepted");bad=r;bad.terrain.flags=3;fail(bad,"truncated terrain granted authority");bad=r;bad.importedTerrain=false;fail(bad,"received bytes claimed imported physics");
    bad=r;bad.motion.flags=MotionReady;fail(bad,"missing real camera accepted");bad=r;bad.motion.flags|=MotionDead;fail(bad,"dead guest retained vehicle control");
    bad=r;bad.motionAgeMs=250;fail(bad,"stale pose kept authority");bad=r;bad.stateAgeMs=750;fail(bad,"stale state kept authority");bad=r;bad.meshAgeMs=1000;fail(bad,"stale deformation kept authority");bad=r;bad.terrainAgeMs=5000;fail(bad,"stale physics terrain kept authority");
    bad=r;bad.focused=false;fail(bad,"background peer kept controls");bad=r;bad.hostCanReplace=false;fail(bad,"native cutscene bypassed");
    bad=r;bad.motion.hostX=254;fail(bad,"vehicle body crossed complete terrain boundary");bad=r;bad.vehicleExtent=0;fail(bad,"unknown hull size fabricated coverage");
    bad=r;bad.motion.velocityX=129;fail(bad,"unbounded vehicle speed accepted");bad=r;bad.motion.hostX=std::numeric_limits<double>::quiet_NaN();fail(bad,"NaN pose accepted");bad=r;bad.motion.fov=170;fail(bad,"invalid camera accepted");
    bad=r;bad.motion.velocityY=1;check(beamngReady(bad,7),"actual airborne physics required fake grounded flag");
    BeamngHeldPresentation presentation;
    check(!presentation.allow(r,7,100,false,false,900),"Ready bootstrapped a camera without an actual body/camera pose");
    check(!presentation.allow(r,7,0,true,false,950),"Ready bootstrapped an unidentified vehicle");
    check(presentation.allow(r,7,100,true,false,975),"actual ready pose could not bootstrap its first exact world pass");
    auto menuRecovery=r;menuRecovery.motion.flags=MotionCameraReady|MotionScreen;menuRecovery.stateFlags|=ScreenOpen;
    BeamngHeldPresentation menuTransition;
    check(!menuTransition.allow(menuRecovery,7,100,true,false,1000,true,true),"selector invented ownership of a never drawn car");
    check(menuTransition.allow(r,7,100,true,true,1100),"ready world pass did not establish actual selector owner");
    auto menuLate=menuRecovery;menuLate.motionAgeMs=250;
    check(!menuTransition.allow(menuLate,7,100,false,false,1350,true,true),"selector drew a stale or absent pose");
    check(menuTransition.allow(menuRecovery,7,100,true,false,1450,true,true)&&!beamngReady(menuRecovery,7),"fresh same-car selector could not rebootstrap after a brief frame gap or acquired physics authority");
    check(!menuTransition.allow(menuRecovery,7,100,true,false,1500,true,false),"selector recovered with a missing screenshot");
    check(menuTransition.allow(menuRecovery,7,100,true,false,1600,true,true),"a brief HUD gap permanently discarded the previously drawn selector car");
    check(!menuTransition.allow(menuRecovery,7,100,true,false,3201,true,true),"selector recovered expired prior world ownership");
    menuTransition.clear();check(menuTransition.allow(r,7,100,true,true,4000),"menu rejection fixture failed to establish ownership");
    auto incompleteMenu=menuRecovery;incompleteMenu.terrain.flags=0;
    check(!menuTransition.allow(incompleteMenu,7,100,true,false,4100,true,true),"selector recovery ignored incomplete terrain");
    check(!menuTransition.allow(menuRecovery,7,101,true,false,4101,true,true),"selector recovery crossed vehicle identity");
    auto heldCar=r;heldCar.motion.flags=MotionCameraReady;heldCar.importedTerrain=false;
    check(!presentation.allow(heldCar,7,100,true,true,1000),"hold bypassed initial physics/terrain readiness");
    check(presentation.allow(r,7,100,true,true,1100),"drawn ready car was not retained");
    check(presentation.allow(heldCar,7,100,true,true,1200)&&!beamngReady(heldCar,7),"short hold either exposed proxy or gained physics authority");
    check(presentation.allow(heldCar,7,100,true,true,3200),"bounded hold expired early");
    check(!presentation.allow(heldCar,7,100,true,true,3201),"physics hold concealed failure indefinitely");
    check(presentation.allow(r,7,100,true,true,3300)&&presentation.allow(heldCar,7,100,true,true,3400),"genuine full readiness did not recover presentation hold");
    check(!presentation.allow(heldCar,7,101,true,true,3401),"hold carried geometry into a different vehicle");
    auto holdFail=[&](BeamngReadiness state,uint32_t room,bool pose,bool drawn,const char* why){
        presentation.clear();check(presentation.allow(r,7,100,true,true,4000),"ready fixture could not arm hold");
        check(!presentation.allow(state,room,100,pose,drawn,4100),why);
    };
    holdFail(heldCar,8,true,true,"hold crossed native room epoch");
    holdFail(heldCar,7,false,true,"hold invented absent visual pose");
    holdFail(heldCar,7,true,false,"hold ignored failed actual world draw");
    bad=heldCar;bad.motionAgeMs=250;holdFail(bad,7,true,true,"hold renewed stale source body");
    bad=heldCar;bad.meshAgeMs=1000;holdFail(bad,7,true,true,"hold renewed stale deformation");
    bad=heldCar;bad.motion.flags=0;holdFail(bad,7,true,true,"hold invented source camera readiness");
    bad=heldCar;bad.connected=false;holdFail(bad,7,true,true,"hold survived source disconnect");
    bad=heldCar;bad.focused=false;holdFail(bad,7,true,true,"hold retained background control");
    bad=heldCar;bad.hostCanReplace=false;holdFail(bad,7,true,true,"hold bypassed native death/menu/cutscene");
    bad=heldCar;bad.stateFlags|=Dead;holdFail(bad,7,true,true,"hold concealed actual death");
    presentation.clear();check(presentation.allow(r,7,100,true,false,5000),"fully ready visual bootstrap rejected");
    check(!presentation.allow(heldCar,7,100,true,true,5100),"undrawn bootstrap armed a continuation");
    auto menu=r;menu.stateFlags=InWorld|ScreenOpen;menu.motion.flags=MotionCameraReady|MotionScreen;
    presentation.clear();check(!presentation.allow(menu,7,100,true,true,5200,true,true),"menu fabricated first vehicle readiness");
    check(presentation.allow(r,7,100,true,true,5300),"ready car could not arm menu presentation");
    check(presentation.allow(menu,7,100,true,true,5400,true,true)&&!beamngReady(menu,7),"selector gained driving physics or exposed proxy");
    check(presentation.allow(menu,7,100,true,true,65400,true,true),"fresh intentional original selector expired at ordinary hold limit");
    auto menuFail=[&](BeamngReadiness state,bool owned,bool ui,const char* why){
        presentation.clear();check(presentation.allow(r,7,100,true,true,66000),"menu rejection setup failed");
        presentation.allow(state,7,100,true,true,66001,owned,ui);
        check(!presentation.allow(state,7,100,true,true,69002,owned,ui),why);
    };
    menuFail(menu,false,true,"unowned UI extended visual hold");menuFail(menu,true,false,"stale UI extended visual hold");
    bad=menu;bad.stateFlags=InWorld;menuFail(bad,true,true,"missing source selector state extended hold");
    bad=menu;bad.motion.flags=MotionCameraReady;menuFail(bad,true,true,"missing source selector motion extended hold");
    bad=menu;bad.importedTerrain=false;menuFail(bad,true,true,"selector bypassed absent imported collision");
    bad=menu;bad.terrainAgeMs=5000;menuFail(bad,true,true,"selector retained stale collision");
    bad=menu;bad.motion.hostX=254;menuFail(bad,true,true,"selector moved car beyond complete collision coverage");
    bad=menu;bad.meshAgeMs=1000;menuFail(bad,true,true,"selector retained stale source geometry");
    bad=menu;bad.motionAgeMs=250;menuFail(bad,true,true,"selector retained stale actual camera");
    auto unsupported=menu;unsupported.meshPresent=false;unsupported.importedTerrain=false;unsupported.rendererReady=false;
    check(beamngSourceMenuOpen(unsupported,7,true)&&!beamngMenuUIReady(unsupported,7,true,false),"late screenshot turned selector text into a mode toggle");
    bad=unsupported;bad.motionAgeMs=1000;check(beamngSourceMenuOpen(bad,7,true),"late camera erased known original textbox state");
    bad=unsupported;bad.stateAgeMs=750;check(!beamngSourceMenuOpen(bad,7,true),"expired source state blocked a new selector toggle");
    check(!beamngSourceMenuOpen(unsupported,8,true)&&!beamngSourceMenuOpen(unsupported,7,false),"old or invalid source state blocked current selector input");
    check(beamngMenuUIReady(unsupported,7,true,true)&&!beamngReady(unsupported,7),"unsupported selected vehicle blocked recovery UI or gained driving authority");
    auto recoveryFail=[&](BeamngReadiness state,uint32_t room,bool valid,bool ui,const char* why){check(!beamngMenuUIReady(state,room,valid,ui),why);};
    recoveryFail(unsupported,7,false,true,"invalid source motion opened recovery UI");recoveryFail(unsupported,7,true,false,"stale original screenshot opened recovery UI");
    recoveryFail(unsupported,8,true,true,"old vehicle epoch opened recovery UI");
    bad=unsupported;bad.focused=false;recoveryFail(bad,7,true,true,"background source took menu input");
    bad=unsupported;bad.connected=false;recoveryFail(bad,7,true,true,"disconnected source opened recovery UI");
    bad=unsupported;bad.hostCanReplace=false;recoveryFail(bad,7,true,true,"recovery UI bypassed native cutscene/death");
    bad=unsupported;bad.motionAgeMs=250;recoveryFail(bad,7,true,true,"stale source session kept menu input");
    bad=unsupported;bad.motion.flags|=MotionDead;recoveryFail(bad,7,true,true,"dead car retained recovery UI");
    BeamngVisualProxyRetention proxy;
    check(!proxy.protects(true,7,true,1000),"unowned hidden player became protected");
    proxy.set(true,7,1000);
    check(proxy.protects(true,7,true,1249),"fresh actual held vehicle lost native HP retention");
    check(!proxy.protects(true,7,true,1250),"stale visual hold protected hidden Leon");
    check(!proxy.protects(true,8,true,1001),"native HP retention crossed epoch");
    check(!proxy.protects(false,7,true,1001),"vehicle hold changed Minecraft damage");
    check(!proxy.protects(true,7,false,1001),"vehicle hold bypassed native menu/death/cutscene");
    check(!proxy.protects(true,7,true,999),"backwards clock renewed HP ownership");
    proxy.set(false,7,1010);check(!proxy.protects(true,7,true,1010),"released visibility retained native HP ownership");
    proxy.set(true,7,1100);proxy.clear();check(!proxy.protects(true,7,true,1100),"restore did not clear native HP ownership");
    proxy.set(true,0,1200);check(!proxy.protects(true,0,true,1200),"zero epoch claimed native HP ownership");
    GuestMotion car=r.motion;car.velocityX=.5f; //10m/s
    Enemy actor{};actor.id=3;actor.flags=ActorAlive|ActorHostile;actor.x=2;actor.minX=actor.minZ=-.3f;actor.maxX=actor.maxZ=.3f;actor.maxY=1.8f;
    GuestMelee contact{};contact.epoch=7;contact.targetId=3;contact.eventId=1;contact.flags=HitVehicle;contact.attackX=1.7;contact.attackY=.8;contact.directionX=1;contact.maxReach=.3f;contact.damage=vehicleImpactDamage(10);contact.knockback=vehicleImpactShove(10);
    check(vehicleImpactInReach(contact,actor,car),"real near contact rejected");
    auto hitFail=[&](GuestMelee hit,Enemy e,GuestMotion m,const char* why){check(!vehicleImpactInReach(hit,e,m),why);};
    auto h=contact;h.flags|=HitProjectile;hitFail(h,actor,car,"mixed vehicle/projectile source accepted");h=contact;h.epoch=6;hitFail(h,actor,car,"wrong-room collision accepted");
    auto e=actor;e.flags=ActorAlive;hitFail(contact,e,car,"vehicle attacked friendly native actor");e=actor;e.flags=ActorAlive|ActorBreakable;check(vehicleImpactInReach(contact,e,car),"actual native crate contact rejected");e.flags|=ActorHostile;hitFail(contact,e,car,"ambiguous crate/hostile record accepted");
    h=contact;h.attackX=4;hitFail(h,actor,car,"distance overlap substituted for actual collision contact");h=contact;h.maxReach=1.01f;hitFail(h,actor,car,"vehicle contact gained melee reach");
    e=actor;e.x=9;h=contact;h.attackX=8.8;hitFail(h,e,car,"contact outside bounded real vehicle footprint accepted");
    h=contact;h.damage=40;hitFail(h,actor,car,"low-speed collision fabricated lethal damage");h=contact;h.knockback=2;hitFail(h,actor,car,"low-speed collision fabricated shove");h=contact;h.directionX=-1;hitFail(h,actor,car,"contact shoved opposite actual velocity");h=contact;h.directionX=0;hitFail(h,actor,car,"missing contact direction accepted");
    auto m=car;m.velocityX=0;hitFail(contact,actor,m,"stationary overlap fabricated impact");m=car;m.flags&=~MotionCameraReady;hitFail(contact,actor,m,"unready vehicle collision accepted");
    VehicleMotionHistory history;GuestMotion witness{};history.push(car,1000);m=car;m.velocityX=0;history.push(m,1050);
    check(history.match(contact,actor,m,1100,witness)&&witness.velocityX==car.velocityX,"stopped post-impact publication discarded actual recent contact velocity");
    check(!history.match(contact,actor,m,1301,witness),"expired pre-impact speed authorized a new stationary contact");
    h=contact;h.directionX=-1;check(!history.match(h,actor,m,1100,witness),"history reversed the actual impact direction");
    auto moved=m;moved.hostX=12;check(!history.match(contact,actor,moved,1100,witness),"old contact retained authority after car moved clear");
    auto otherRoom=m;otherRoom.epoch=8;h=contact;h.epoch=8;check(!history.match(h,actor,otherRoom,1100,witness),"old room speed authorized a new epoch contact");
    history.clear();history.push(m,1400);check(!history.match(contact,actor,m,1401,witness),"stationary history fabricated collision velocity");
    h=contact;h.damage=vehicleImpactDamage(4.4704);h.knockback=vehicleImpactShove(4.4704);check(vehicleKnockdownImpact(h),"actual ten-mph impact missed native knockdown threshold");
    h.damage=vehicleImpactDamage(4.4);h.knockback=vehicleImpactShove(4.4);check(!vehicleKnockdownImpact(h),"slow contact requested high-speed knockdown");
    h=contact;h.flags=HitProjectile;check(!vehicleKnockdownImpact(h),"Minecraft projectile gained vehicle knockdown policy");
    auto shove=vehicleShoveMetres(contact);check(std::abs(shove.x-.8f)<1e-5f&&shove.y==0&&shove.z==0,"shove mixed native scale or injected vertical motion");
    h=contact;h.directionX=.8f;h.directionY=.6f;shove=vehicleShoveMetres(h);check(std::abs(shove.x-.8f)<1e-5f&&shove.y==0,"hill contact lifted native actor through terrain");
    VehicleContactLedger ledger;check(ledger.accept(7,3,1,1000),"first actual contact rejected");check(!ledger.accept(7,3,1,1500),"contact event replay accepted");check(!ledger.accept(7,3,2,1249),"per-node storm double-damaged actor");check(ledger.accept(7,3,2,1250),"250ms actual follow-up collision rejected");check(ledger.accept(7,4,3,1250),"distinct actual target shared cooldown");check(!ledger.accept(7,3,4,1200),"backwards monotonic clock accepted");check(ledger.accept(8,3,1,2000),"new room could not reset event sequence");ledger.clear();check(ledger.accept(8,3,1,2001),"inactive authority retained contact replay ownership");
    ledger.clear();for(unsigned id=1;id<=64;++id)check(ledger.accept(7,id,id,1000),"bounded contact capacity rejected");check(!ledger.accept(7,65,65,1000),"contact storm exceeded capacity");check(ledger.accept(7,65,65,3000),"expired contact slots did not recover");
    std::puts("PASS: mutually exclusive F7/F8 mode transitions, stale/complete terrain and real-pose gates, isolated inputs, physical vehicle contact bounds/speed/friendly/crate routing, replay/rate and horizontal shove.");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"BeamNG mode fixture failed: %s\n",e.what());return 1;}}
