#include "camera_control.hpp"
#include "hook_support.hpp"
#include "vendor/minhook/include/MinHook.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

// Hook signature/layout from re4_tweaks CameraSmooth SDK and Game.cpp.
// The native view/projection calculation still runs after this hook.
namespace re4craft { namespace {
using SmoothMove=void(__fastcall*)(void*,void*,CameraPoint*);
SmoothMove originalSmooth{};
CameraControlBridge* cameraBridge{};
void __fastcall smoothMove(void* self,void* unused,CameraPoint* point) {
    originalSmooth(self,unused,point);
    if(cameraBridge)cameraBridge->apply(self,point);
}
constexpr float radians=3.14159265358979323846f/180.f;
float wrapYaw(float yaw){return std::remainder(yaw,360.f);}
bool finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
}
bool calculateCameraPoint(const CameraSettings& s,Vec3 feet,CameraPoint& out){
    if(!finite(feet)||!std::isfinite(s.yawDegrees)||!std::isfinite(s.pitchDegrees)||!std::isfinite(s.eyeHeightBlocks)||!std::isfinite(s.worldUnitsPerBlock)||!std::isfinite(s.thirdPersonDistanceBlocks)||!std::isfinite(s.fovDegrees))return false;
    if(s.worldUnitsPerBlock<=0||s.worldUnitsPerBlock>=10000||s.eyeHeightBlocks<0||s.eyeHeightBlocks>3||s.thirdPersonDistanceBlocks<0||s.thirdPersonDistanceBlocks>16||s.fovDegrees<20||s.fovDegrees>140||static_cast<uint32_t>(s.mode)>2)return false;
    if(s.guestCameraValid){
        if(!finite(s.guestCameraPosition)||!std::isfinite(s.guestYawDegrees)||!std::isfinite(s.guestPitchDegrees)||!std::isfinite(s.guestRollDegrees)||std::abs(s.guestPitchDegrees)>90||std::abs(s.guestRollDegrees)>360)return false;
        float yaw=wrapYaw(s.guestYawDegrees)*radians,pitch=s.guestPitchDegrees*radians;
        Vec3 direction{-std::sin(yaw)*std::cos(pitch),-std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
        out.position=s.guestCameraPosition;out.target={out.position.x+direction.x*s.worldUnitsPerBlock,out.position.y+direction.y*s.worldUnitsPerBlock,out.position.z+direction.z*s.worldUnitsPerBlock};out.roll=s.guestRollDegrees*radians;out.fovDegrees=s.fovDegrees;return finite(out.target);
    }
    float yaw=wrapYaw(s.yawDegrees)*radians,pitch=std::clamp(s.pitchDegrees,-89.f,89.f)*radians;
    Vec3 look{-std::sin(yaw)*std::cos(pitch),-std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
    Vec3 eye=feet;eye.y+=s.eyeHeightBlocks*s.worldUnitsPerBlock;
    out={};out.fovDegrees=s.fovDegrees;
    if(s.mode==CameraMode::FirstPerson){out.position=eye;out.target={eye.x+look.x*s.worldUnitsPerBlock,eye.y+look.y*s.worldUnitsPerBlock,eye.z+look.z*s.worldUnitsPerBlock};}
    else{float distance=s.thirdPersonDistanceBlocks*s.worldUnitsPerBlock*(s.mode==CameraMode::ThirdPersonFront?1.f:-1.f);out.position={eye.x+look.x*distance,eye.y+look.y*distance,eye.z+look.z*distance};out.target=eye;}
    return finite(out.position)&&finite(out.target);
}
InputLook advanceNativeLook(InputLook current,InputMouseDelta delta,float nativeTime,bool allowLook,bool rawAvailable,uint64_t lastRawTime,uint64_t now){
    if(!allowLook||(rawAvailable&&now-lastRawTime<=50))return current;
    if(!std::isfinite(delta.x)||!std::isfinite(delta.y)||std::abs(delta.x)>=10000||std::abs(delta.y)>=10000||!std::isfinite(nativeTime))return current;
    float step=.095492966f*std::clamp(nativeTime,.1f,4.f);
    return {wrapYaw(current.yawDegrees+delta.x*step),std::clamp(current.pitchDegrees+delta.y*step,-89.f,89.f)};
}
bool CameraControlBridge::installHooks(GameAccess* game,LogFn log){
    if(installed_)return game_==game;
    if(!game||!game->initialized()||cameraBridge)return false;
    hookdetail::ImageCode code;if(!code.initialize())return false;
    uintptr_t match=code.find("56 B9 ? ? ? ? E8 ? ? ? ? 8D 53 60");
    uintptr_t target=match?code.callTarget(match+6):0;
    uintptr_t mouse=code.find("DB 05 ? ? ? ? D9 45 ? D9 C0 DE CA D9 C5");
    if(!target||!mouse){if(log)log("Native camera signatures missing or altered; camera takeover disabled.");return false;}
    // Require the verified thiscall prologue; an unknown mod's jump is not followed.
    const uint8_t prologue[]{0x55,0x8b,0xec,0x83,0xec,0x54};uint8_t actual[6]{};
    if(!hookdetail::copy(target,actual,6)||std::memcmp(actual,prologue,6)){if(log)log("Native camera routine already altered; camera takeover disabled.");return false;}
    mouseDelta_=hookdetail::operand(mouse+2);
    if(!hookdetail::memory(mouseDelta_,8))return false;
    auto initialized=MH_Initialize();if(initialized!=MH_OK&&initialized!=MH_ERROR_ALREADY_INITIALIZED)return false;
    game_=game;cameraBridge=this;
    if(MH_CreateHook(reinterpret_cast<void*>(target),reinterpret_cast<void*>(smoothMove),reinterpret_cast<void**>(&originalSmooth))!=MH_OK){cameraBridge=nullptr;return false;}
    if(MH_EnableHook(reinterpret_cast<void*>(target))!=MH_OK){MH_RemoveHook(reinterpret_cast<void*>(target));cameraBridge=nullptr;return false;}
    installed_=true;
    if(log){char message[160]{};std::snprintf(message,sizeof(message),"Native camera hook installed before world view calculation at %08X.",static_cast<unsigned>(target));log(message);}return true;
}
void CameraControlBridge::setState(const CameraSettings& state){
    std::lock_guard lock(mutex_);
    bool rising=state.active&&!settings_.active;
    float yaw=settings_.yawDegrees,pitch=settings_.pitchDegrees;
    settings_=state;
    if(rising){yaw=std::isfinite(state.yawDegrees)?wrapYaw(state.yawDegrees):0.f;pitch=std::isfinite(state.pitchDegrees)?std::clamp(state.pitchDegrees,-89.f,89.f):0.f;rawAvailable_=false;lastRawTime_=0;}
    settings_.yawDegrees=yaw;settings_.pitchDegrees=pitch;lastStateTime_=GetTickCount64();
    if(!state.active){rawSincePoll_=false;appliedVehiclePresentations_.clear();if(game_)restoreYawLocked(game_->sample());}
}
void CameraControlBridge::addMouseDelta(float dx,float dy,float sensitivity){
    if(!std::isfinite(dx)||!std::isfinite(dy)||!std::isfinite(sensitivity)||std::abs(dx)>10000||std::abs(dy)>10000||sensitivity<=0||sensitivity>5)return;
    std::lock_guard lock(mutex_);if(!settings_.active)return;
    settings_.yawDegrees=wrapYaw(settings_.yawDegrees+dx*sensitivity);settings_.pitchDegrees=std::clamp(settings_.pitchDegrees+dy*sensitivity,-89.f,89.f);rawSincePoll_=true;rawAvailable_=true;lastRawTime_=GetTickCount64();
}
InputLook CameraControlBridge::look()const{std::lock_guard lock(mutex_);return {settings_.yawDegrees,settings_.pitchDegrees};}
InputMouseDelta CameraControlBridge::readMouseDelta()const{int32_t delta[2]{};if(!mouseDelta_||!hookdetail::copy(mouseDelta_,delta,8)||std::abs(static_cast<int64_t>(delta[0]))>=10000||std::abs(static_cast<int64_t>(delta[1]))>=10000)return {};return {float(delta[0]),float(-delta[1])};}
void CameraControlBridge::restore(){CameraSettings state{};setState(state);}
void CameraControlBridge::restoreYawLocked(const NativeSnapshot& native){
    // Do not overwrite a new heading chosen by a cutscene or native routine.
    if(ownedYaw_&&native.playerAddress==playerWithYaw_&&std::abs(std::remainder(native.angles.y-lastWrittenYaw_,2*3.14159265358979323846f))<.001f)hookdetail::write(playerWithYaw_+0xa4,&originalYaw_,4);
    ownedYaw_=false;playerWithYaw_=0;
}
void CameraControlBridge::onInputPoll(bool allowLook){
    std::lock_guard lock(mutex_);
    if(!game_)return;auto native=game_->sample();
    if(!settings_.active||GetTickCount64()-lastStateTime_>750||!native.canReplace()){rawSincePoll_=false;restoreYawLocked(native);return;}
    // Raw delivery may stop after an input-device/focus transition. Recent
    // raw events suppress their DirectInput copy; an expired source permits
    // the game's verified deltas again instead of permanently freezing look.
    const uint64_t now=GetTickCount64();
    if(allowLook&&!(rawAvailable_&&now-lastRawTime_<=50)){
        auto look=advanceNativeLook({settings_.yawDegrees,settings_.pitchDegrees},readMouseDelta(),native.deltaTime,allowLook,rawAvailable_,lastRawTime_,now);
        settings_.yawDegrees=look.yawDegrees;settings_.pitchDegrees=look.pitchDegrees;
    }
    rawSincePoll_=false;
    if(!ownedYaw_||playerWithYaw_!=native.playerAddress){originalYaw_=native.angles.y;playerWithYaw_=native.playerAddress;ownedYaw_=true;}
    float heading=-settings_.yawDegrees*radians;if(hookdetail::write(native.playerAddress+0xa4,&heading,4))lastWrittenYaw_=heading;
}
void CameraControlBridge::apply(void* smooth,CameraPoint* nativePoint){
    std::lock_guard lock(mutex_);if(!game_||!smooth)return;
    auto native=game_->sample();bool active=settings_.active&&GetTickCount64()-lastStateTime_<=750&&native.canReplace();
    CameraPoint point{};
    if(active){if(!calculateCameraPoint(settings_,native.position,point))return;}
    else{if(!wasApplied_)return;if(!hookdetail::copy(reinterpret_cast<uintptr_t>(nativePoint),&point,sizeof(point))||!finite(point.position)||!finite(point.target))return;}
    // CameraControl::move copies m_effect_100 to its CAMERA, then native
    // CameraCalc builds the view and projection and copies the result to pG.
    uintptr_t object=reinterpret_cast<uintptr_t>(smooth);
    if(!hookdetail::memory(object+0x100,sizeof(CameraPoint),true)||!hookdetail::memory(object+0x130,sizeof(CameraPoint),true))return;
    const bool effectWritten=hookdetail::write(object+0x100,&point,sizeof(point));
    const bool stateWritten=hookdetail::write(object+0x130,&point,sizeof(point));wasApplied_=active;
    if(active&&effectWritten&&stateWritten&&settings_.vehiclePresentationValid){
        appliedVehiclePresentations_.record(settings_.vehiclePresentation,
            {point.position.x,point.position.y,point.position.z},{point.target.x,point.target.y,point.target.z},point.fovDegrees,vehiclePresentationNowMs());
    }
}
bool CameraControlBridge::appliedVehiclePresentation(const NativeSnapshot& native,uint32_t epoch,VehiclePresentationSample& output)const{
    std::lock_guard lock(mutex_);
    return settings_.active&&native.valid&&appliedVehiclePresentations_.find(epoch,
        {native.cameraPosition.x,native.cameraPosition.y,native.cameraPosition.z},
        {native.cameraTarget.x,native.cameraTarget.y,native.cameraTarget.z},
        {native.view[1][0],native.view[1][1],native.view[1][2]},native.fovDegrees,vehiclePresentationNowMs(),output);
}
AppliedVehicleCameraDiagnostic CameraControlBridge::appliedVehicleDiagnostic(const NativeSnapshot& native,uint32_t epoch,bool& active)const{
    std::lock_guard lock(mutex_);active=settings_.active;
    return appliedVehiclePresentations_.diagnose(epoch,
        {native.cameraPosition.x,native.cameraPosition.y,native.cameraPosition.z},
        {native.cameraTarget.x,native.cameraTarget.y,native.cameraTarget.z},
        {native.view[1][0],native.view[1][1],native.view[1][2]},native.fovDegrees,vehiclePresentationNowMs());
}
}
