#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <chrono>

namespace re4craft {
struct VehicleVector { double x{},y{},z{}; };
struct VehicleQuaternion { float x{},y{},z{},w{1.f}; };
inline bool validVehicleQuaternion(VehicleQuaternion q) {
    const double norm=double(q.x)*q.x+double(q.y)*q.y+double(q.z)*q.z+double(q.w)*q.w;
    return std::isfinite(norm)&&std::abs(norm-1.)<=.02;
}
inline VehicleQuaternion normalizedVehicleQuaternion(VehicleQuaternion q) {
    const double length=std::sqrt(double(q.x)*q.x+double(q.y)*q.y+double(q.z)*q.z+double(q.w)*q.w);
    return {float(q.x/length),float(q.y/length),float(q.z/length),float(q.w/length)};
}
inline VehicleQuaternion vehicleQuaternionProduct(VehicleQuaternion a,VehicleQuaternion b) {
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
            a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
            a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
            a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
inline VehicleQuaternion vehicleQuaternionDelta(VehicleQuaternion presented,VehicleQuaternion captured) {
    captured=normalizedVehicleQuaternion(captured);
    return normalizedVehicleQuaternion(vehicleQuaternionProduct(normalizedVehicleQuaternion(presented),{-captured.x,-captured.y,-captured.z,captured.w}));
}
inline VehicleVector vehicleRotate(VehicleQuaternion q,VehicleVector v) {
    const double tx=2*(q.y*v.z-q.z*v.y),ty=2*(q.z*v.x-q.x*v.z),tz=2*(q.x*v.y-q.y*v.x);
    return {v.x+q.w*tx+q.y*tz-q.z*ty,v.y+q.w*ty+q.z*tx-q.x*tz,v.z+q.w*tz+q.x*ty-q.y*tx};
}
inline VehicleQuaternion vehicleSlerp(VehicleQuaternion a,VehicleQuaternion b,double fraction) {
    a=normalizedVehicleQuaternion(a);b=normalizedVehicleQuaternion(b);
    double dot=double(a.x)*b.x+double(a.y)*b.y+double(a.z)*b.z+double(a.w)*b.w;
    if(dot<0){b={-b.x,-b.y,-b.z,-b.w};dot=-dot;}
    double left=1-fraction,right=fraction;
    if(dot<.9995){const double angle=std::acos(std::clamp(dot,-1.,1.)),sine=std::sin(angle);left=std::sin((1-fraction)*angle)/sine;right=std::sin(fraction*angle)/sine;}
    return normalizedVehicleQuaternion({float(a.x*left+b.x*right),float(a.y*left+b.y*right),float(a.z*left+b.z*right),float(a.w*left+b.w*right)});
}
inline VehicleVector vehicleLerp(VehicleVector a,VehicleVector b,double fraction) {
    return {a.x+(b.x-a.x)*fraction,a.y+(b.y-a.y)*fraction,a.z+(b.z-a.z)*fraction};
}
inline double vehiclePresentationNowMs() {
    // steady_clock uses QPC on Windows. GetTickCount64's 10-16 ms steps are
    // appropriate for freshness gates, but visibly quantize a 60 Hz transform.
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
struct VehicleCameraAngles { float yaw{},pitch{},roll{}; };
inline VehicleCameraAngles vehicleCameraAngles(VehicleQuaternion q) {
    q=normalizedVehicleQuaternion(q);
    const auto f=vehicleRotate(q,{0,0,1}),up=vehicleRotate(q,{0,1,0});
    constexpr double degrees=180./3.14159265358979323846;
    const double yaw=std::atan2(-f.x,f.z),pitch=-std::asin(std::clamp(f.y,-1.,1.));
    const VehicleVector right{std::cos(yaw),0,std::sin(yaw)};
    const VehicleVector zeroUp{f.y*right.z-f.z*right.y,f.z*right.x-f.x*right.z,f.x*right.y-f.y*right.x};
    const double sine=up.x*right.x+up.y*right.y+up.z*right.z;
    const double cosine=up.x*zeroUp.x+up.y*zeroUp.y+up.z*zeroUp.z;
    // RE4 CameraCalc rotates its level up around position-target (backwards),
    // using the positive axis-angle passed in CAMERA_POINT.roll. Negating
    // this angle mirrors the bank even when position/target/FOV match exactly.
    return {float(yaw*degrees),float(pitch*degrees),float(std::atan2(sine,cosine)*degrees)};
}
struct VehiclePresentationSample {
    uint32_t epoch{},vehicleId{};uint64_t tick{};
    double sourceSeconds{}; // actual body snapshot time, which may repeat
    VehicleVector position{},cameraPosition{};
    VehicleQuaternion orientation{},cameraOrientation{};
    float fov{70};uint32_t cameraMode{1};
};
inline bool validVehiclePresentationSample(const VehiclePresentationSample& s) {
    if(!s.epoch||!s.vehicleId||!s.tick||!std::isfinite(s.sourceSeconds)||s.sourceSeconds<0||
       !validVehicleQuaternion(s.orientation)||!validVehicleQuaternion(s.cameraOrientation)||
       !std::isfinite(s.fov)||s.fov<20||s.fov>140||s.cameraMode>2)return false;
    for(double value:{s.position.x,s.position.y,s.position.z,s.cameraPosition.x,s.cameraPosition.y,s.cameraPosition.z})
        if(!std::isfinite(value)||std::abs(value)>=100000)return false;
    return true;
}
// The body follows actual source snapshot times, rather than network arrival
// times or repeated cached snapshots. User camera changes still have their own
// arrival timeline, expressed relative to the body so both move coherently.
// These bounded histories never predict or change authoritative physics.
class VehiclePresentation {
public:
    static constexpr double DelayMs=35,MaxDelayMs=50,CameraDelayMs=20,MaxAgeMs=250,MaxGapMs=150;
    void clear(){count_=bodyCount_=0;clockStarted_=false;delayMs_=DelayMs;jitterMs_=0;clockRate_=1;}
    double bodyDelayMs()const{return delayMs_;}
    bool push(VehiclePresentationSample s,double arrivalMs) {
        if(!validVehiclePresentationSample(s)||!std::isfinite(arrivalMs)||arrivalMs<0)return false;
        s.orientation=normalizedVehicleQuaternion(s.orientation);s.cameraOrientation=normalizedVehicleQuaternion(s.cameraOrientation);
        if(count_){
            const auto& last=history_[count_-1];
            if(s.epoch!=last.sample.epoch){if(uint32_t(s.epoch-last.sample.epoch)>=0x80000000u)return false;clear();}
            else if(s.vehicleId!=last.sample.vehicleId)clear();
            else {
                if(s.tick<=last.sample.tick||s.sourceSeconds<last.sample.sourceSeconds||arrivalMs<last.arrival)return false;
                const auto delta=VehicleVector{s.position.x-last.sample.position.x,s.position.y-last.sample.position.y,s.position.z-last.sample.position.z};
                if(s.cameraMode!=last.sample.cameraMode||arrivalMs-last.arrival>MaxGapMs||
                   (s.sourceSeconds-last.sample.sourceSeconds)*1000.>MaxGapMs||delta.x*delta.x+delta.y*delta.y+delta.z*delta.z>64.)clear();
            }
        }
        const double sourceMs=s.sourceSeconds*1000.;
        if(!count_)clockOffset_=arrivalMs-sourceMs;
        // Minimum transit/snapshot age is a clock offset observation; a late
        // packet must not move the playout clock backwards. Phase correction
        // below is gradual, so discovering a fresher packet cannot cause a jump.
        clockOffset_=std::min(clockOffset_,arrivalMs-sourceMs);
        if(!bodyCount_||sourceMs>body_[bodyCount_-1].arrival){
            if(bodyCount_){
                // Delivery variation, rather than cached-body duplicates,
                // determines the small extra interpolation reserve.
                const double variation=std::abs((arrivalMs-lastBodyArrival_)-(sourceMs-body_[bodyCount_-1].arrival));
                jitterMs_+=.15*(std::min(variation,MaxGapMs)-jitterMs_);
                delayMs_=std::clamp(DelayMs+1.5*jitterMs_,DelayMs,MaxDelayMs);
            }
            lastBodyArrival_=arrivalMs;
            append(body_,bodyCount_,{s,sourceMs});
            while(bodyCount_>2&&sourceMs-body_[1].arrival>500.)removeFirst(body_,bodyCount_);
        }
        // Camera collision clipping, orbit, zoom and look are actual source
        // samples. Remove their sampled body transform before interpolation.
        const VehicleQuaternion inverse{-s.orientation.x,-s.orientation.y,-s.orientation.z,s.orientation.w};
        auto relative=s;
        relative.cameraPosition=vehicleRotate(inverse,{s.cameraPosition.x-s.position.x,s.cameraPosition.y-s.position.y,s.cameraPosition.z-s.position.z});
        relative.cameraOrientation=normalizedVehicleQuaternion(vehicleQuaternionProduct(inverse,s.cameraOrientation));
        if(count_&&arrivalMs==history_[count_-1].arrival)history_[count_-1]={relative,arrivalMs};
        else append(history_,count_,{relative,arrivalMs});
        while(count_>2&&arrivalMs-history_[1].arrival>500.)removeFirst(history_,count_);
        return true;
    }
    bool sample(double nowMs,VehiclePresentationSample& out) {
        if(!count_||!std::isfinite(nowMs)||nowMs<history_[count_-1].arrival||
           nowMs-history_[count_-1].arrival>=MaxAgeMs||(clockStarted_&&nowMs<lastNow_))return false;
        const double desired=nowMs-clockOffset_-delayMs_;
        if(!clockStarted_){playout_=std::min(desired,body_[bodyCount_-1].arrival);clockStarted_=true;}
        else {
            const double elapsed=nowMs-lastNow_;
            // Recover a brief buffer starvation within hundreds of milliseconds,
            // rather than retaining its latency for seconds at a 2% slew. Both
            // playback speed and its rate of change are bounded; no time jump,
            // backwards motion or physics extrapolation is permitted.
            const double error=desired-(playout_+elapsed);
            const double targetRate=std::clamp(1.+error/200.,.85,1.35);
            clockRate_+=std::clamp(targetRate-clockRate_,-elapsed*.0015,elapsed*.0015);
            playout_=std::max(playout_,std::min(playout_+elapsed*clockRate_,body_[bodyCount_-1].arrival));
        }
        lastNow_=nowMs;
        out=interpolate(body_,bodyCount_,playout_);
        const auto camera=interpolate(history_,count_,nowMs-CameraDelayMs);
        const auto offset=vehicleRotate(out.orientation,camera.cameraPosition);
        out.cameraPosition={out.position.x+offset.x,out.position.y+offset.y,out.position.z+offset.z};
        out.cameraOrientation=normalizedVehicleQuaternion(vehicleQuaternionProduct(out.orientation,camera.cameraOrientation));
        out.fov=camera.fov;out.cameraMode=camera.cameraMode;
        return true;
    }
private:
    struct Stamped {VehiclePresentationSample sample;double arrival{};};
    using History=std::array<Stamped,64>;
    static void removeFirst(History& h,size_t& count){std::move(h.begin()+1,h.begin()+count,h.begin());--count;}
    static void append(History& h,size_t& count,Stamped s){if(count==h.size())removeFirst(h,count);h[count++]=s;}
    static VehiclePresentationSample interpolate(const History& h,size_t count,double target){
        if(target<=h[0].arrival)return h[0].sample;
        for(size_t i=1;i<count;++i)if(target<=h[i].arrival){
            const auto& a=h[i-1];const auto& b=h[i];const double fraction=(target-a.arrival)/(b.arrival-a.arrival);
            auto s=b.sample;s.sourceSeconds=a.sample.sourceSeconds+(b.sample.sourceSeconds-a.sample.sourceSeconds)*fraction;
            s.position=vehicleLerp(a.sample.position,b.sample.position,fraction);
            s.orientation=vehicleSlerp(a.sample.orientation,b.sample.orientation,fraction);
            s.cameraPosition=vehicleLerp(a.sample.cameraPosition,b.sample.cameraPosition,fraction);
            s.cameraOrientation=vehicleSlerp(a.sample.cameraOrientation,b.sample.cameraOrientation,fraction);
            s.fov=float(a.sample.fov+(b.sample.fov-a.sample.fov)*fraction);return s;
        }
        return h[count-1].sample; // hold the last actual sample; never extrapolate
    }
    History history_{},body_{};size_t count_{},bodyCount_{};
    double clockOffset_{},playout_{},lastNow_{},lastBodyArrival_{},jitterMs_{},delayMs_{DelayMs},clockRate_{1};bool clockStarted_{};
};
// Camera calculation and EndScene do not share a cadence, and the native
// render queue may still contain an older view. Retain the body paired with
// each camera that was actually applied, then match the enqueue-time camera
// tuple rather than advancing the car against a different camera frame.
struct AppliedVehicleCameraDiagnostic {
    size_t total{},sameEpoch{},fresh{};
    bool nativeValid{},candidate{},candidateFresh{};
    uint64_t tick{};double ageMs{},positionDelta{},targetDelta{},fovDelta{},upDot{},nativeUpLength{};
    VehicleVector position{},target{},up{};float fov{};
};
class AppliedVehiclePresentationHistory {
public:
    void clear(){count_=0;}
    void record(const VehiclePresentationSample& s,VehicleVector cameraPosition,VehicleVector cameraTarget,float fov,double nowMs){
        if(!validVehiclePresentationSample(s)||!std::isfinite(nowMs))return;
        if(count_==entries_.size()){std::move(entries_.begin()+1,entries_.end(),entries_.begin());--count_;}
        entries_[count_++]={s,cameraPosition,cameraTarget,fov,nowMs};
    }
    bool find(uint32_t epoch,VehicleVector cameraPosition,VehicleVector cameraTarget,VehicleVector cameraUp,float fov,double nowMs,VehiclePresentationSample& out)const{
        if(!epoch||!std::isfinite(fov)||!std::isfinite(nowMs))return false;
        const double upLength=std::sqrt(cameraUp.x*cameraUp.x+cameraUp.y*cameraUp.y+cameraUp.z*cameraUp.z);
        if(!std::isfinite(upLength)||upLength<.9||upLength>1.1)return false;
        for(size_t i=count_;i>0;--i){const auto& e=entries_[i-1];
            if(e.sample.epoch!=epoch||nowMs<e.applied||nowMs-e.applied>=VehiclePresentation::MaxAgeMs)continue;
            const auto up=vehicleRotate(e.sample.cameraOrientation,{0,1,0});
            const double upDot=(up.x*cameraUp.x+up.y*cameraUp.y+up.z*cameraUp.z)/upLength;
            if(close(e.position,cameraPosition)&&close(e.target,cameraTarget)&&std::abs(double(e.fov)-fov)<=.005&&upDot>1.-1e-5){out=e.sample;return true;}
        }
        return false;
    }
    AppliedVehicleCameraDiagnostic diagnose(uint32_t epoch,VehicleVector position,VehicleVector target,VehicleVector up,float fov,double nowMs)const{
        AppliedVehicleCameraDiagnostic result{};result.total=count_;
        result.nativeUpLength=std::sqrt(up.x*up.x+up.y*up.y+up.z*up.z);
        result.nativeValid=epoch&&std::isfinite(nowMs)&&std::isfinite(fov)&&std::isfinite(result.nativeUpLength)
            &&result.nativeUpLength>=.9&&result.nativeUpLength<=1.1;
        for(double v:{position.x,position.y,position.z,target.x,target.y,target.z})result.nativeValid&=std::isfinite(v);
        if(!epoch||!std::isfinite(nowMs))return result;
        auto difference=[](VehicleVector a,VehicleVector b){return std::max({std::abs(a.x-b.x),std::abs(a.y-b.y),std::abs(a.z-b.z)});};
        double best=0;
        for(size_t i=count_;i>0;--i){const auto& e=entries_[i-1];
            if(e.sample.epoch!=epoch)continue;++result.sameEpoch;
            const bool fresh=nowMs>=e.applied&&nowMs-e.applied<VehiclePresentation::MaxAgeMs;
            if(fresh)++result.fresh;
            const double pd=difference(position,e.position),td=difference(target,e.target);
            const double score=result.nativeValid?pd+td:0;
            // Prefer fresh same-epoch frames, then the closest authored tuple;
            // ties retain the newest frame. This is diagnostic selection only.
            if(result.candidate&&((!fresh&&result.candidateFresh)||(fresh==result.candidateFresh&&score>=best)))continue;
            result.candidate=true;result.candidateFresh=fresh;best=score;
            result.tick=e.sample.tick;result.ageMs=nowMs-e.applied;result.positionDelta=pd;result.targetDelta=td;
            result.fovDelta=std::abs(double(e.fov)-fov);result.position=e.position;result.target=e.target;result.fov=e.fov;
            result.up=vehicleRotate(e.sample.cameraOrientation,{0,1,0});
            result.upDot=result.nativeUpLength>0?(result.up.x*up.x+result.up.y*up.y+result.up.z*up.z)/result.nativeUpLength:0;
        }
        return result;
    }
private:
    static bool close(VehicleVector a,VehicleVector b){
        // Native fields and the applied CameraPoint are float copies. This
        // small absolute tolerance permits their last float operation while
        // keeping separate moving views distinct; it is 1 mm at 50 units/m.
        return std::abs(a.x-b.x)<=.05&&std::abs(a.y-b.y)<=.05&&std::abs(a.z-b.z)<=.05;
    }
    struct Entry{VehiclePresentationSample sample;VehicleVector position,target;float fov{};double applied{};};
    std::array<Entry,64> entries_{};size_t count_{};
};
}
