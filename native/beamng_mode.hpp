#pragma once
#include "../protocol/re4craft_protocol.h"
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <array>
#include <cstddef>

namespace re4craft {
inline bool beamngUiMatches(bool valid,uint32_t ageMs,bool modeKnown,bool capturedMenu,bool currentMenu){
    return valid&&ageMs<500&&modeKnown&&capturedMenu==currentMenu;
}
enum class BridgeMode : uint8_t { Native=0, Minecraft=1, BeamNG=2, Halo=3, Ultrakill=4 };
constexpr std::array<BridgeMode,4> GuestBridgeModes{BridgeMode::Minecraft,BridgeMode::BeamNG,BridgeMode::Halo,BridgeMode::Ultrakill};
constexpr size_t GuestBackendCount=GuestBridgeModes.size();
constexpr bool guestBridgeMode(BridgeMode mode){
    for(auto guest:GuestBridgeModes)if(mode==guest)return true;
    return false;
}
constexpr bool validBridgeMode(BridgeMode mode) {
    return mode==BridgeMode::Native||guestBridgeMode(mode);
}
constexpr size_t guestBackendIndex(BridgeMode mode){
    for(size_t i=0;i<GuestBackendCount;++i)if(GuestBridgeModes[i]==mode)return i;
    return GuestBackendCount;
}
// Multiple edges may arrive during one frame. Retaining the current choice
// avoids changing authority according to whichever key was checked first.
constexpr BridgeMode selectedBridgeMode(BridgeMode current,bool f4Edge,bool f6Edge,bool f7Edge,bool f8Edge){
    if(!validBridgeMode(current))current=BridgeMode::Native;
    const unsigned count=unsigned(f4Edge)+unsigned(f6Edge)+unsigned(f7Edge)+unsigned(f8Edge);
    if(count!=1)return current;
    const auto selected=f4Edge?BridgeMode::Ultrakill:f6Edge?BridgeMode::Halo:f7Edge?BridgeMode::BeamNG:BridgeMode::Minecraft;
    return current==selected?BridgeMode::Native:selected;
}
constexpr BridgeMode selectedBridgeMode(BridgeMode current,bool f7Edge,bool f8Edge) {
    return selectedBridgeMode(current,false,false,f7Edge,f8Edge);
}
constexpr bool minecraftGameplayRoutes(BridgeMode mode){return mode==BridgeMode::Minecraft;}
constexpr bool modernGuestMode(BridgeMode mode){return mode==BridgeMode::Halo||mode==BridgeMode::Ultrakill;}
// Separate policies retain the established Minecraft and BeamNG bounds.
// ULTRAKILL transfer remains disabled until a reviewed source motion limit is
// added; a fast character controller must not silently widen another mode.
constexpr float guestMotionVelocityLimit(BridgeMode mode){
    return mode==BridgeMode::Minecraft||mode==BridgeMode::Halo?16.f:mode==BridgeMode::BeamNG?128.f:0.f;
}
inline Input inactiveBridgeInput(Input input) {
    input.flags=0;input.held=0;input.pressed=0;input.scroll=0;input.mouseButtons=0;
    input.selectedSlot=UINT32_MAX;return input;
}

// Halo/ULTRAKILL's source contract may assert MotionReady only after actually
// constructing this epoch's complete native collider and checking the entire
// actual player capsule. Replacing it immediately revokes readiness. Merely
// receiving Collision bytes, or drawing original assets, grants no authority.
struct SourceGuestReadiness {
    BridgeMode mode{BridgeMode::Native};
    bool connected{},hostCanReplace{},focused{},rendererReady{},meshPresent{},completeTerrain{};
    uint64_t stateAgeMs{},meshAgeMs{},motionAgeMs{},terrainAgeMs{};
    uint32_t stateFlags{},meshEpoch{};
    GuestMotion motion{};
    Collision terrain{};
};
inline bool sourceGuestReady(const SourceGuestReadiness& r,uint32_t epoch){
    const float limit=guestMotionVelocityLimit(r.mode);
    if(!modernGuestMode(r.mode)||limit<=0||!epoch||!r.connected||!r.hostCanReplace||!r.focused||
       !r.rendererReady||!r.meshPresent||!r.completeTerrain||r.stateAgeMs>=750||r.meshAgeMs>=1000||
       r.motionAgeMs>=250||r.terrainAgeMs>=2000||r.meshEpoch!=epoch||
       (r.stateFlags&(InWorld|Dead|ScreenOpen))!=InWorld)return false;
    const auto& m=r.motion;const auto& c=r.terrain;
    if(m.epoch!=epoch||(m.flags&(MotionReady|MotionCameraReady|MotionDead|MotionScreen))!=(MotionReady|MotionCameraReady)||
       (m.flags&~31U)||m.reserved||m.cameraMode>2||c.epoch!=epoch||c.flags!=1||c.reserved||
       !c.triangleCount||c.triangleCount>200000||!std::isfinite(c.radius)||c.radius<=8||c.radius>256)return false;
    for(double v:{m.hostX,m.hostY,m.hostZ,m.cameraX,m.cameraY,m.cameraZ})if(!std::isfinite(v)||std::abs(v)>=100000.)return false;
    for(float v:{m.velocityX,m.velocityY,m.velocityZ,m.yaw,m.pitch,m.health,m.maxHealth,m.cameraRoll,m.fov,
                 c.centerX,c.centerY,c.centerZ})if(!std::isfinite(v))return false;
    if(std::abs(m.velocityX)>limit||std::abs(m.velocityY)>limit||std::abs(m.velocityZ)>limit||std::abs(m.yaw)>=1e7f||
       std::abs(m.pitch)>90||std::abs(m.cameraRoll)>360||m.fov<20||m.fov>140||m.health<=0||m.health>m.maxHealth||m.maxHealth<=0||m.maxHealth>1024)return false;
    // This is a conservative transfer margin, not an invented actor radius.
    // The source separately proves that its actual capsule has full coverage.
    const double remaining=double(c.radius)-8.;
    for(const auto& p:std::initializer_list<std::array<double,3>>{{m.hostX,m.hostY,m.hostZ},{m.cameraX,m.cameraY,m.cameraZ}}){
        const double dx=p[0]-c.centerX,dy=p[1]-c.centerY,dz=p[2]-c.centerZ;
        if(dx*dx+dy*dy+dz*dz>remaining*remaining)return false;
    }
    return true;
}

// Ages are monotonic transport ages in milliseconds. importedTerrain means the
// guest actually constructed its collision mesh, rather than merely receiving
// bytes. The vehicle producer must withhold MotionReady until that succeeds.
struct BeamngReadiness {
    bool connected{},hostCanReplace{},focused{},rendererReady{},meshPresent{},importedTerrain{};
    uint64_t stateAgeMs{},meshAgeMs{},motionAgeMs{},terrainAgeMs{};
    uint32_t stateFlags{},meshEpoch{};
    GuestMotion motion{};
    Collision terrain{};
    float vehicleExtent{}; // measured attached reference-cluster extent, not a guessed car size
};
inline bool beamngFreshPresentation(const BeamngReadiness& r,uint32_t epoch) {
    if(!r.connected||!r.hostCanReplace||!r.focused||!r.rendererReady||!r.meshPresent||
       r.stateAgeMs>=750||r.meshAgeMs>=1000||r.motionAgeMs>=250||
       !(r.stateFlags&InWorld)||(r.stateFlags&Dead)||r.meshEpoch!=epoch)return false;
    const auto& m=r.motion;
    if(m.epoch!=epoch||!(m.flags&MotionCameraReady)||
       (m.flags&~31U)||(m.flags&MotionDead)||m.reserved||m.cameraMode>2)return false;
    for(double v:{m.hostX,m.hostY,m.hostZ,m.cameraX,m.cameraY,m.cameraZ})
        if(!std::isfinite(v)||std::abs(v)>=100000.)return false;
    for(float v:{m.velocityX,m.velocityY,m.velocityZ,m.yaw,m.pitch,m.health,m.maxHealth,m.cameraRoll,m.fov})
        if(!std::isfinite(v))return false;
    if(std::abs(m.velocityX)>128||std::abs(m.velocityY)>128||std::abs(m.velocityZ)>128||std::abs(m.yaw)>=1e7f||
       std::abs(m.pitch)>90||std::abs(m.cameraRoll)>360||m.fov<20||m.fov>140||m.health<0||m.health>m.maxHealth||m.maxHealth<=0||m.maxHealth>1024)return false;
    return true;
}
inline bool beamngTerrainCoverage(const BeamngReadiness& r,uint32_t epoch) {
    if(!r.importedTerrain||r.terrainAgeMs>=5000)return false;
    const auto& m=r.motion;
    const auto& c=r.terrain;
    if(c.epoch!=epoch||c.flags!=1||c.reserved||!c.triangleCount||c.triangleCount>200000||
       !std::isfinite(c.radius)||c.radius<=0||c.radius>256||!std::isfinite(r.vehicleExtent)||r.vehicleExtent<=0||r.vehicleExtent>8)return false;
    for(float v:{c.centerX,c.centerY,c.centerZ})if(!std::isfinite(v)||std::abs(v)>=100000.f)return false;
    const double dx=m.hostX-c.centerX,dy=m.hostY-c.centerY,dz=m.hostZ-c.centerZ;
    const double remaining=double(c.radius)-r.vehicleExtent;
    return remaining>0&&dx*dx+dy*dy+dz*dz<=remaining*remaining;
}
inline bool beamngReady(const BeamngReadiness& r,uint32_t epoch) {
    return beamngFreshPresentation(r,epoch)&&beamngTerrainCoverage(r,epoch)&&(r.motion.flags&MotionReady);
}
// Recovery UI can remain usable when a chosen stock vehicle exceeds our mesh
// or map limits. It grants no avatar visibility, driving or combat authority.
inline bool beamngSourceMenuOpen(const BeamngReadiness& r,uint32_t epoch,bool stateAndMotionValid){
    return stateAndMotionValid&&r.connected&&r.stateAgeMs<750&&r.motion.epoch==epoch&&
        (r.stateFlags&(InWorld|ScreenOpen|Dead))==(InWorld|ScreenOpen);
}
inline bool beamngMenuUIReady(const BeamngReadiness& r,uint32_t epoch,bool stateAndMotionValid,bool hudFresh){
    return beamngSourceMenuOpen(r,epoch,stateAndMotionValid)&&hudFresh&&r.hostCanReplace&&r.focused&&
        r.motionAgeMs<250&&
        (r.motion.flags&(MotionScreen|MotionCameraReady|MotionDead))==(MotionScreen|MotionCameraReady);
}
// A short actual-source physics hold must not reveal the native proxy. Only a
// previously drawn, fully ready car can continue, with fresh source geometry,
// pose and camera. This never grants physics, contact or movement authority.
class BeamngHeldPresentation {
public:
    static constexpr uint64_t MaxHoldMs=2000;
    void clear(){epoch_=vehicle_=0;holdStarted_=drawnAt_=0;holding_=false;}
    bool allow(const BeamngReadiness& r,uint32_t epoch,uint32_t vehicle,bool actualPose,bool worldFresh,uint64_t now,bool menuOpen=false,bool hudFresh=false){
        if(beamngReady(r,epoch)){
            // The world pass pairs this exact body/camera pose with the
            // authored native camera. Motion alone cannot bootstrap that pair.
            if(!actualPose||!vehicle){clear();return false;}
            holding_=false;
            if(actualPose&&worldFresh&&vehicle){epoch_=epoch;vehicle_=vehicle;drawnAt_=now;}
            return true;
        }
        const bool knownDraw=vehicle&&epoch_==epoch&&vehicle_==vehicle&&now>=drawnAt_&&now-drawnAt_<=MaxHoldMs;
        const bool currentMenu=menuOpen&&hudFresh&&(r.stateFlags&ScreenOpen)&&(r.motion.flags&MotionScreen);
        // A real selector transition can briefly stall source/readback frames.
        // Re-author the camera only from a fresh actual pose of the already
        // drawn car. The world pass must still validate that exact new pair;
        // this visual bootstrap never grants driving/contact authority.
        if(knownDraw&&currentMenu&&actualPose&&beamngFreshPresentation(r,epoch)&&beamngTerrainCoverage(r,epoch)){
            if(worldFresh)drawnAt_=now;
            holding_=false;return true;
        }
        if(!actualPose||!worldFresh||!vehicle||epoch_!=epoch||vehicle_!=vehicle||!beamngFreshPresentation(r,epoch)){
            if(!knownDraw||!r.connected||!r.hostCanReplace)clear();
            return false;
        }
        // The original source selector deliberately pauses driving physics.
        // Retain only the already drawn car while that owned UI, the actual
        // camera, geometry, world pass and imported terrain remain fresh.
        if(menuOpen&&hudFresh&&(r.stateFlags&ScreenOpen)&&(r.motion.flags&MotionScreen)&&beamngTerrainCoverage(r,epoch)){
            drawnAt_=now;holding_=false;return true;
        }
        drawnAt_=now;
        if(!holding_){holdStarted_=now;holding_=true;}
        return now>=holdStarted_&&now-holdStarted_<=MaxHoldMs;
    }
private:
    uint32_t epoch_{},vehicle_{};uint64_t holdStarted_{},drawnAt_{};bool holding_{};
};
// Protect only the hidden native player's HP while a validated visual hold is
// renewed. This state never enables movement, enemy damage or contact ledgers.
class BeamngVisualProxyRetention {
public:
    void clear(){retained_=false;epoch_=0;renewed_=0;}
    void set(bool retained,uint32_t epoch,uint64_t now){retained_=retained&&epoch;epoch_=epoch;renewed_=now;}
    bool protects(bool vehicleMode,uint32_t epoch,bool nativeCanReplace,uint64_t now)const{
        return retained_&&vehicleMode&&epoch_==epoch&&nativeCanReplace&&now>=renewed_&&now-renewed_<250;
    }
private:
    bool retained_{};uint32_t epoch_{};uint64_t renewed_{};
};
}
