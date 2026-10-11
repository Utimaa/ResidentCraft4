#include "camera_control.hpp"
#include "host_controls.hpp"
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
using namespace re4craft;
void check(bool result,const char* message){if(!result)throw std::runtime_error(message);}
bool near(float a,float b){return std::abs(a-b)<.02f;}
namespace {
VehicleVector normalize(VehicleVector v){double n=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);return {v.x/n,v.y/n,v.z/n};}
VehicleVector cross(VehicleVector a,VehicleVector b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
double dot(VehicleVector a,VehicleVector b){return a.x*b.x+a.y*b.y+a.z*b.z;}
VehicleQuaternion capturedCameraQuaternion(VehicleVector forward,VehicleVector up){
    forward=normalize(forward);const auto right=normalize(cross(up,forward));up=normalize(cross(forward,right));
    const double m00=right.x,m01=up.x,m02=forward.x,m10=right.y,m11=up.y,m12=forward.y,m20=right.z,m21=up.z,m22=forward.z;
    double x,y,z,w;const double trace=m00+m11+m22;
    if(trace>0){double s=std::sqrt(trace+1)*2;w=s/4;x=(m21-m12)/s;y=(m02-m20)/s;z=(m10-m01)/s;}
    else if(m00>m11&&m00>m22){double s=std::sqrt(1+m00-m11-m22)*2;x=s/4;y=(m01+m10)/s;z=(m02+m20)/s;w=(m21-m12)/s;}
    else if(m11>m22){double s=std::sqrt(1+m11-m00-m22)*2;y=s/4;x=(m01+m10)/s;z=(m12+m21)/s;w=(m02-m20)/s;}
    else{double s=std::sqrt(1+m22-m00-m11)*2;z=s/4;x=(m02+m20)/s;y=(m12+m21)/s;w=(m10-m01)/s;}
    return normalizedVehicleQuaternion({float(x),float(y),float(z),float(w)});
}
VehicleVector originalCameraCalcUp(const CameraPoint& point){
    // Independent numeric model of original CameraCalc59E660: subtract
    // position-target (VectorSubtract9762E0), cross levelY/backward, then
    // AxisRotation972C70 and MtxVector972540 rotate level up by point.roll.
    const auto backward=normalize({double(point.position.x)-point.target.x,double(point.position.y)-point.target.y,double(point.position.z)-point.target.z});
    const auto right=normalize(cross({0,1,0},backward)),level=normalize(cross(backward,right));
    const auto spin=cross(backward,level);const double c=std::cos(point.roll),s=std::sin(point.roll),projection=dot(backward,level)*(1-c);
    return normalize({level.x*c+spin.x*s+backward.x*projection,level.y*c+spin.y*s+backward.y*projection,level.z*c+spin.z*s+backward.z*projection});
}
void actualBankedTupleTests(){
    struct Tuple{Vec3 position,target;VehicleVector authoredUp,oldNativeUp;};
    // Exact rejected same-position/target/FOV tuples, current-session log
    // 21:44:15 epoch5 ticks1760/1770; their bank was reflected by the old sign.
    const Tuple tuples[]{
        {{-80024.492188f,2398.430664f,-11437.708984f},{-80022.625000f,2119.633545f,-12398.057617f},
            {-.005371690,.960333235,-.278803199},{.006454757,.960333288,-.278780103}},
        {{-80028.937500f,2399.354492f,-11437.688477f},{-80026.515625f,2120.485840f,-12398.014648f},
            {-.006739051,.960302640,-.278878866},{.008146621,.960302651,-.278841287}}
    };
    for(const auto& tuple:tuples){
        const auto q=capturedCameraQuaternion({double(tuple.target.x)-tuple.position.x,double(tuple.target.y)-tuple.position.y,double(tuple.target.z)-tuple.position.z},tuple.authoredUp);
        const auto angles=vehicleCameraAngles(q);const auto expected=vehicleRotate(q,{0,1,0});
        for(auto mode:{CameraMode::FirstPerson,CameraMode::ThirdPersonBack,CameraMode::ThirdPersonFront}){
            CameraSettings s{};s.active=s.guestCameraValid=s.vehiclePresentationValid=true;s.mode=mode;
            s.guestCameraPosition=tuple.position;s.guestYawDegrees=angles.yaw;s.guestPitchDegrees=angles.pitch;s.guestRollDegrees=angles.roll;s.fovDegrees=70;
            CameraPoint point{};check(calculateCameraPoint(s,{},point),"captured banked camera rejected");
            check(near(point.target.x,tuple.target.x)&&near(point.target.y,tuple.target.y)&&near(point.target.z,tuple.target.z)&&near(point.fovDegrees,70),"bank fix changed camera target or FOV");
            const auto actual=originalCameraCalcUp(point);check(dot(actual,normalize(expected))>1.-1e-7,"original native camera bank disagrees with source quaternion");
            auto mirrored=point;mirrored.roll=-point.roll;const auto old=originalCameraCalcUp(mirrored);
            check(dot(old,normalize(tuple.oldNativeUp))>1.-1e-7,"old roll sign does not reproduce actual rejected native up");
            check(dot(old,normalize(expected))<1.-1e-5,"captured old-sign tuple did not reproduce strict pairing failure");
            VehiclePresentationSample sample{};sample.epoch=5;sample.vehicleId=42;sample.tick=1760;sample.sourceSeconds=1;
            sample.cameraOrientation=q;AppliedVehiclePresentationHistory history;
            history.record(sample,{point.position.x,point.position.y,point.position.z},{point.target.x,point.target.y,point.target.z},70,1000);
            VehiclePresentationSample found{};
            check(history.find(5,{point.position.x,point.position.y,point.position.z},{point.target.x,point.target.y,point.target.z},actual,70,1020,found),"correct original native roll did not satisfy strict pairing");
            check(!history.find(5,{point.position.x,point.position.y,point.position.z},{point.target.x,point.target.y,point.target.z},old,70,1020,found),"strict pairing accepted mirrored native roll");
        }
    }
    // Both bank directions and q/-q representations share the native convention.
    for(float bank:{-60.f,60.f}){
        const double angle=bank*3.14159265358979323846/360.;const VehicleQuaternion q{0,0,float(std::sin(angle)),float(std::cos(angle))};
        const auto a=vehicleCameraAngles(q),b=vehicleCameraAngles({-q.x,-q.y,-q.z,-q.w});
        check(near(a.roll,-bank)&&near(a.roll,b.roll),"bank sign or quaternion equivalence failed");
    }
}
}
int main(){try{
    CameraSettings settings{};settings.active=true;settings.mode=CameraMode::FirstPerson;CameraPoint point{};
    check(calculateCameraPoint(settings,{10,20,30},point),"first-person calculation rejected");
    check(near(point.position.x,10)&&near(point.position.y,1640)&&near(point.position.z,30)&&near(point.target.z,1030),"first-person eye/forward incorrect");
    settings.yawDegrees=90;check(calculateCameraPoint(settings,{0,0,0},point)&&near(point.target.x,-1000)&&near(point.target.z,0),"Minecraft positive yaw did not turn toward -X");
    settings.yawDegrees=0;settings.pitchDegrees=45;check(calculateCameraPoint(settings,{0,0,0},point)&&point.target.y<point.position.y,"positive Minecraft pitch did not look down");
    settings.pitchDegrees=0;settings.mode=CameraMode::ThirdPersonBack;check(calculateCameraPoint(settings,{0,0,0},point)&&near(point.position.z,-4000)&&near(point.target.z,0),"rear camera incorrect");
    settings.mode=CameraMode::ThirdPersonFront;check(calculateCameraPoint(settings,{0,0,0},point)&&near(point.position.z,4000)&&near(point.target.z,0),"front camera incorrect");
    settings.eyeHeightBlocks=1.27f;check(calculateCameraPoint(settings,{0,50,0},point)&&near(point.position.y,1320),"crouching camera height incorrect");
    settings.guestCameraValid=true;settings.guestCameraPosition={100,200,300};settings.guestYawDegrees=180;settings.guestPitchDegrees=30;settings.guestRollDegrees=10;
    check(calculateCameraPoint(settings,{0,0,0},point)&&near(point.position.y,200)&&point.target.z<300&&point.target.y<200&&near(point.roll,10*3.14159265f/180),"vanilla guest camera position/orientation/roll ignored");
    actualBankedTupleTests();
    settings.guestCameraPosition.x=std::numeric_limits<float>::infinity();check(!calculateCameraPoint(settings,{0,0,0},point),"nonfinite guest camera accepted");settings.guestCameraValid=false;
    settings.worldUnitsPerBlock=0;check(!calculateCameraPoint(settings,{0,0,0},point),"invalid scale accepted");settings.worldUnitsPerBlock=1000;
    settings.yawDegrees=std::numeric_limits<float>::quiet_NaN();check(!calculateCameraPoint(settings,{0,0,0},point),"nonfinite look accepted");settings.yawDegrees=0;
    CameraControlBridge bridge;bridge.setState(settings);bridge.addMouseDelta(20,10,.15f);auto look=bridge.look();check(near(look.yawDegrees,3)&&near(look.pitchDegrees,1.5f),"raw look delta incorrect");
    bridge.setState(settings);look=bridge.look();check(near(look.yawDegrees,3),"frame state reset accumulated look");
    bridge.addMouseDelta(0,1000,1);check(near(bridge.look().pitchDegrees,89),"pitch clamp failed");bridge.restore();bridge.addMouseDelta(50,50);check(near(bridge.look().yawDegrees,3),"inactive mouse changed look");
    settings.yawDegrees=40;settings.pitchDegrees=-20;bridge.setState(settings);look=bridge.look();check(near(look.yawDegrees,40)&&near(look.pitchDegrees,-20),"reactivation did not seed native direction");
    InputLook seed{10,20};InputMouseDelta nativeDelta{10,5};
    auto polled=advanceNativeLook(seed,nativeDelta,1,true,false,0,150);
    check(near(polled.yawDegrees,10.95493f)&&near(polled.pitchDegrees,20.47746f),"native fallback delta/sign conversion failed without raw input");
    polled=advanceNativeLook(seed,nativeDelta,1,true,true,100,150);
    check(near(polled.yawDegrees,10)&&near(polled.pitchDegrees,20),"50ms raw delivery boundary double-counted native deltas");
    polled=advanceNativeLook(seed,nativeDelta,1,true,true,100,151);
    check(near(polled.yawDegrees,10.95493f)&&near(polled.pitchDegrees,20.47746f),"expired raw source permanently froze native fallback");
    polled=advanceNativeLook(polled,nativeDelta,1,true,true,100,10000);
    check(near(polled.yawDegrees,11.90986f),"stale raw source did not continue native look on later polls");
    polled=advanceNativeLook(seed,nativeDelta,1,false,true,100,10000);
    check(near(polled.yawDegrees,10)&&near(polled.pitchDegrees,20),"GUI-only poll changed camera look");
    polled=advanceNativeLook(seed,nativeDelta,std::numeric_limits<float>::quiet_NaN(),true,false,0,10000);
    check(near(polled.yawDegrees,10)&&near(polled.pitchDegrees,20),"invalid native timestep poisoned look");
    constexpr uint32_t display=0x81523456U;
    check(refreshOwnedPlayerDisplayFlags(display,false)==display,"pre-draw refresh changed an unowned display bit");
    check(refreshOwnedPlayerDisplayFlags(display,true)==(display|GameAccess::HidePlayerMask),"owned pre-draw refresh changed unrelated display flags");
    check(playerDisplayFlags(display|GameAccess::HidePlayerMask,false)==display,"restoring visible player changed unrelated display flags");
    check(playerDisplayFlags(display,true)==(display|GameAccess::HidePlayerMask),"restoring an originally hidden player lost original hide value");
    check(canRefreshPlayerHide(true,100,350,true,true)&&!canRefreshPlayerHide(true,100,351,true,true),"pre-draw visibility watchdog boundary incorrect");
    check(!canRefreshPlayerHide(false,100,150,true,true)&&!canRefreshPlayerHide(true,100,150,false,true)&&!canRefreshPlayerHide(true,100,150,true,false),"pre-draw hide crossed ownership/native-phase/global-allocation boundary");
    const uint64_t all=UINT64_MAX;check(filterHostButtons(all,false)==all,"disabled bridge changed native input");check((filterHostButtons(all,true)&HostBlockedButtons)==0,"native fire/aim/inventory not blocked");
    constexpr uint64_t preserved=0xfULL|0x40ULL|0x400ULL|0x1000ULL|0x2000ULL|0x40000000ULL|0x80000000ULL|0x4000000000ULL;
    check(filterHostButtons(preserved,true)==preserved,"bridge blocked movement/interaction/pause/scripted input");
    check(blockNativeSubscreen(1,true)&&blockNativeSubscreen(2,true)&&blockNativeSubscreen(4,true),"manual native briefcase/map not blocked");
    check(!blockNativeSubscreen(1,false)&&!blockNativeSubscreen(0x10,true)&&!blockNativeSubscreen(0x20,true)&&!blockNativeSubscreen(0x80,true),"inactive/shop/save/scripted native screen blocked");
    std::puts("PASS: camera modes/yaw/pitch, stale-raw/native fallback and GUI gating, owned pre-draw hide/restore/freshness, native action mask and scripted-screen preservation.");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"Control fixture failed: %s\n",error.what());return 1;}}
