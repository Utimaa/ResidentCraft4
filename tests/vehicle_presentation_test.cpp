#include "../native/vehicle_presentation.hpp"
#include <cstdio>
#include <limits>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>
using namespace re4craft;
namespace {
unsigned checks{};
#define CHECK(expression) do{++checks;if(!(expression)){std::printf("FAIL line %u: %s\n",__LINE__,#expression);return 1;}}while(false)
bool near(double a,double b,double tolerance=1e-5){return std::abs(a-b)<tolerance;}
VehiclePresentationSample pose(uint64_t tick,double x,double yaw=0){
    const double a=yaw*3.14159265358979323846/360.;VehiclePresentationSample s{};
    s.epoch=s.vehicleId=1;s.tick=tick;s.sourceSeconds=tick*.02;s.position={x,0,0};
    s.orientation={0,float(std::sin(a)),0,float(std::cos(a))};s.cameraOrientation=s.orientation;
    const auto offset=vehicleRotate(s.orientation,{0,2,-6});s.cameraPosition={x+offset.x,offset.y,offset.z};return s;
}
// Retain the old arrival-based sampler solely as an independent replay control.
VehiclePresentationSample oldSample(const std::vector<std::pair<double,VehiclePresentationSample>>& h,double now){
    const double target=now-50.;if(target<=h.front().first)return h.front().second;
    for(size_t i=1;i<h.size();++i)if(target<=h[i].first){const auto& a=h[i-1];const auto& b=h[i];
        double t=(target-a.first)/(b.first-a.first);auto s=b.second;
        s.sourceSeconds=a.second.sourceSeconds+(b.second.sourceSeconds-a.second.sourceSeconds)*t;
        s.position=vehicleLerp(a.second.position,b.second.position,t);return s;}
    return h.back().second;
}
int replay(const char* path){
    // Exact received records converted from the diagnostic peer's JSON;
    // no invented source samples, authority, or ground-truth FPS claim.
    std::ifstream file(path);if(!file)return 2;
    std::vector<std::pair<double,VehiclePresentationSample>> events;std::string line;
    while(std::getline(file,line)){
        for(auto& c:line)if(c==',')c=' ';std::istringstream row(line);double arrival{};VehiclePresentationSample s{};
        if(!(row>>arrival>>s.epoch>>s.vehicleId>>s.tick>>s.sourceSeconds>>s.position.x>>s.position.y>>s.position.z
             >>s.orientation.x>>s.orientation.y>>s.orientation.z>>s.orientation.w>>s.cameraPosition.x>>s.cameraPosition.y>>s.cameraPosition.z
             >>s.cameraOrientation.x>>s.cameraOrientation.y>>s.cameraOrientation.z>>s.cameraOrientation.w>>s.fov>>s.cameraMode))return 3;
        events.push_back({arrival,s});
    }
    if(events.size()<10)return 4;
    VehiclePresentation p;VehiclePresentationSample nowSample{},previous{},oldPrevious{};size_t next{};bool hadPrevious{};
    std::vector<std::pair<double,VehiclePresentationSample>> oldHistory;
    size_t frames{},freshHolds{},oldHolds{};double squared{},oldSquared{},speedPrevious{},oldSpeedPrevious{};
    double minimumSource=events.front().second.sourceSeconds,maximumSource=minimumSource;
    for(double now=events.front().first;now<=events.back().first;now+=1000./60.){
        while(next<events.size()&&events[next].first<=now){const auto& e=events[next++];
            if(!p.push(e.second,e.first))return 5;
            if(!oldHistory.empty()&&(e.second.epoch!=oldHistory.back().second.epoch||e.second.vehicleId!=oldHistory.back().second.vehicleId||e.second.cameraMode!=oldHistory.back().second.cameraMode||e.first-oldHistory.back().first>150.)){
                oldHistory.clear();hadPrevious=false;minimumSource=e.second.sourceSeconds;
            }
            maximumSource=e.second.sourceSeconds;oldHistory.push_back(e);
            while(oldHistory.size()>2&&e.first-oldHistory[1].first>500.)oldHistory.erase(oldHistory.begin());
        }
        if(oldHistory.empty()||!p.sample(now,nowSample)){hadPrevious=false;continue;}
        if(nowSample.sourceSeconds<minimumSource-1e-8||nowSample.sourceSeconds>maximumSource+1e-8)return 6;
        const auto old=oldSample(oldHistory,now);
        if(hadPrevious&&nowSample.sourceSeconds>=previous.sourceSeconds&&nowSample.vehicleId==previous.vehicleId&&nowSample.cameraMode==previous.cameraMode){
            auto distance=[](VehicleVector a,VehicleVector b){return std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z));};
            const double speed=distance(nowSample.position,previous.position)*60.,oldSpeed=distance(old.position,oldPrevious.position)*60.;
            if(speed<1e-8)++freshHolds;if(oldSpeed<1e-8)++oldHolds;
            squared+=(speed-speedPrevious)*(speed-speedPrevious);oldSquared+=(oldSpeed-oldSpeedPrevious)*(oldSpeed-oldSpeedPrevious);
            speedPrevious=speed;oldSpeedPrevious=oldSpeed;++frames;
        }
        previous=nowSample;oldPrevious=old;hadPrevious=true;
    }
    if(!frames)return 7;
    std::printf("Recorded replay PASS: %zu real packets, %zu presentation frames; held translation frames old=%zu new=%zu; per-frame speed-change RMS old=%.6f new=%.6f host-units/s (recorded physics, not invented ground truth).\n",events.size(),frames,oldHolds,freshHolds,std::sqrt(oldSquared/frames),std::sqrt(squared/frames));return 0;
}
}
int main(int argc,char** argv){
    if(argc==3&&std::string(argv[1])=="--replay")return replay(argv[2]);
    VehiclePresentation p;VehiclePresentationSample out{};
    CHECK(!p.sample(100,out));
    auto a=pose(1,0,170),b=pose(2,2,-170);a.sourceSeconds=1.;b.sourceSeconds=1.04;
    CHECK(p.push(a,100.25)&&p.push(b,140.25)&&p.sample(155.25,out));
    CHECK(near(out.position.x,1));
    const auto f=vehicleRotate(out.orientation,{0,0,1});CHECK(std::abs(f.x)<1e-5&&near(f.z,-1));
    const auto inverse=VehicleQuaternion{-out.orientation.x,-out.orientation.y,-out.orientation.z,out.orientation.w};
    const auto local=vehicleRotate(inverse,{out.cameraPosition.x-out.position.x,out.cameraPosition.y-out.position.y,out.cameraPosition.z-out.position.z});
    CHECK(near(local.x,0)&&near(local.y,2)&&near(local.z,-6));
    CHECK(p.sample(340.25,out)&&near(out.position.x,2));
    CHECK(!p.sample(390.25,out)&&!p.sample(139,out));
    auto sameTime=pose(3,2,-170);sameTime.sourceSeconds=b.sourceSeconds;sameTime.cameraPosition.x+=1;
    CHECK(p.push(sameTime,160.25));
    auto bad=sameTime;CHECK(!p.push(bad,180));
    bad.tick=4;bad.sourceSeconds=.01;CHECK(!p.push(bad,180));
    auto next=pose(4,40);next.sourceSeconds=1.06;
    CHECK(p.push(next,180.25)&&p.sample(180.25,out)&&near(out.position.x,40));
    next=pose(5,41);next.sourceSeconds=1.08;next.cameraMode=0;
    CHECK(p.push(next,200.25)&&p.sample(200.25,out)&&out.cameraMode==0&&near(out.position.x,41));
    next=pose(6,42);next.sourceSeconds=1.10;next.vehicleId=2;
    CHECK(p.push(next,220.25)&&p.sample(220.25,out)&&near(out.position.x,42));
    next.epoch=2;next.tick=1;CHECK(p.push(next,240.25)&&p.sample(240.25,out)&&out.epoch==2);
    next.epoch=1;next.tick=7;CHECK(!p.push(next,260));
    next.epoch=2;next.tick=2;next.orientation.w=0;CHECK(!p.push(next,260));
    next=pose(2,0);next.position.x=std::numeric_limits<double>::quiet_NaN();CHECK(!p.push(next,300));
    CHECK(!p.push(pose(3,0),std::numeric_limits<double>::quiet_NaN()));
    auto delta=vehicleQuaternionDelta(pose(1,0,90).orientation,pose(1,0,45).orientation);
    auto corrected=vehicleRotate(delta,vehicleRotate(pose(1,0,45).orientation,{0,0,1}));CHECK(near(corrected.x,1)&&near(corrected.z,0));
    auto angles=vehicleCameraAngles(pose(1,0,90).orientation);CHECK(near(angles.yaw,-90)&&near(angles.pitch,0)&&near(angles.roll,0));
    p.clear();CHECK(!p.sample(400,out));
    for(uint64_t i=1;i<=200;i++)CHECK(p.push(pose(i,double(i)),i*20.));
    CHECK(p.sample(4000,out)&&near(out.position.x,198.25));
    CHECK(near(p.bodyDelayMs(),35));

    // Sub-millisecond transforms move on QPC without repeated 15.625 ms steps.
    p.clear();a=pose(1,0);b=pose(2,2);a.sourceSeconds=10;b.sourceSeconds=10.04;
    CHECK(p.push(a,1000.25)&&p.push(b,1040.25)&&p.sample(1055.25,out));const double before=out.position.x;
    CHECK(p.sample(1055.75,out)&&near(out.position.x-before,.025,1e-5));
    CHECK(!p.sample(1055.50,out));

    // Cached body snapshots still permit genuine stationary-camera controls.
    p.clear();a=pose(1,3,90);b=a;b.tick=2;b.cameraPosition=vehicleLerp(a.cameraPosition,a.position,.5);
    CHECK(p.push(a,1000.1)&&p.push(b,1050.1)&&p.sample(1070.1,out));
    CHECK(near(out.position.x,3)&&near(out.position.z,0)&&near(out.cameraPosition.x,b.cameraPosition.x));

    // Uneven arrivals and duplicated snapshots cannot separate an orbit camera
    // from the smoothly turning car or manufacture a standstill/catch-up cycle.
    std::vector<std::pair<double,VehiclePresentationSample>> events;
    constexpr double jitter[]{0,8,31,4,29,16};
    for(unsigned i=0;i<100;i++){
        const double source=20.+i*.04;auto s=pose(i+1,(source-20.)*10.,(source-20.)*20.);s.sourceSeconds=source;
        const double arrival=5000.125+i*40+jitter[i%6];events.push_back({arrival,s});
        if(i<99){s.tick=1000+i;const double after=5000.125+(i+1)*40+jitter[(i+1)%6];events.push_back({(arrival+after)*.5,s});}
    }
    p.clear();size_t index{};bool previousValid{};double previousX{};unsigned smoothFrames{};double oldMaxSpeedChange{},newMaxSpeedChange{},oldLastSpeed{},newLastSpeed{};
    std::vector<std::pair<double,VehiclePresentationSample>> old;
    for(double now=5000.125;now<8900;now+=1000./60.){
        while(index<events.size()&&events[index].first<=now){auto e=events[index++];e.second.tick=index;CHECK(p.push(e.second,e.first));old.push_back(e);}
        if(!p.sample(now,out))continue;
        const auto q=VehicleQuaternion{-out.orientation.x,-out.orientation.y,-out.orientation.z,out.orientation.w};
        const auto offset=vehicleRotate(q,{out.cameraPosition.x-out.position.x,out.cameraPosition.y-out.position.y,out.cameraPosition.z-out.position.z});
        CHECK(near(offset.x,0,3e-5)&&near(offset.y,2,3e-5)&&near(offset.z,-6,3e-5));
        if(previousValid&&now>6000){const double speed=(out.position.x-previousX)*60.;
            // A short buffer may hold during a genuine 67 ms delivery gap.
            // It may not extrapolate, rewind or recover with an unbounded jump.
            CHECK(speed>=-.0001&&speed<=13.5001);++smoothFrames;
            CHECK(out.sourceSeconds<=events[index-1].second.sourceSeconds+1e-8);
            CHECK(p.bodyDelayMs()>=35&&p.bodyDelayMs()<=50);
            newMaxSpeedChange=std::max(newMaxSpeedChange,std::abs(speed-newLastSpeed));newLastSpeed=speed;
            const double oldSpeed=(oldSample(old,now).position.x-oldSample(old,now-1000./60.).position.x)*60.;
            oldMaxSpeedChange=std::max(oldMaxSpeedChange,std::abs(oldSpeed-oldLastSpeed));oldLastSpeed=oldSpeed;}
        previousX=out.position.x;previousValid=true;
    }
    CHECK(smoothFrames>150&&newMaxSpeedChange<oldMaxSpeedChange*.8);
    CHECK(p.sample(8900,out));const double previousSource=out.sourceSeconds;
    CHECK(p.sample(8900.5,out)&&out.sourceSeconds>=previousSource&&out.sourceSeconds-previousSource<=.000676);
    CHECK(!p.sample(9200,out));CHECK(std::isfinite(vehiclePresentationNowMs()));
    // Genuine GE samples at 50 Hz need only a 35 ms body reserve. Camera
    // controls take 20 ms, composed against that same rendered body transform.
    p.clear();
    for(unsigned i=0;i<=100;i++){
        auto s=pose(i+1,i*.2);s.sourceSeconds=30+i*.02;
        s.cameraPosition.x=s.position.x+i*.01;
        CHECK(p.push(s,10000+i*20.)&&p.sample(10000+i*20.,out));
        if(i>10){
            CHECK(near((30+i*.02-out.sourceSeconds)*1000,35));
            CHECK(near(out.cameraPosition.x-out.position.x,(i-1)*.01));
        }
    }
    // A real 100 ms hole contains no new body to draw. Hold the newest actual
    // pose, then recover the lost reserve smoothly instead of retaining >100 ms
    // of latency for seconds. The source time remains bounded by received data.
    p.clear();unsigned nextArrival{};double previousTime{},latestSource{};bool hadTime{};
    for(unsigned frame=0;frame<=90;frame++){
        const double now=frame*1000./60.;
        while(nextArrival<=now){
            if(nextArrival<400||nextArrival>=500){
                auto s=pose(nextArrival+1,nextArrival*.01);s.sourceSeconds=40+nextArrival*.001;
                CHECK(p.push(s,nextArrival));latestSource=s.sourceSeconds;
            }
            nextArrival+=20;
        }
        CHECK(p.sample(now,out));
        CHECK(out.sourceSeconds<=latestSource+1e-8&&p.bodyDelayMs()>=35&&p.bodyDelayMs()<=50);
        if(hadTime)CHECK(out.sourceSeconds>=previousTime&&out.sourceSeconds-previousTime<=1.35/60.+1e-8);
        if(now>=1000)CHECK((40+now*.001-out.sourceSeconds)*1000<50);
        previousTime=out.sourceSeconds;hadTime=true;
    }
    CHECK(!p.sample(1800,out));
    // A queued native view must keep its own applied car pose even after a
    // later camera is applied; publishing pending EndScene settings is not an
    // application. Match position, target and FOV with epoch/age bounds.
    AppliedVehiclePresentationHistory applied;
    a=pose(1,1);b=pose(2,2);
    applied.record(a,{100,200,300},{100,200,1300},70,1000.25);
    applied.record(b,{110,200,300},{110,200,1300},70,1016.9);
    CHECK(applied.find(1,{100,200,300},{100,200,1300},{0,1,0},70,1030.2,out)&&near(out.position.x,1));
    CHECK(applied.find(1,{110,200,300},{110,200,1300},{0,1,0},70,1030.2,out)&&near(out.position.x,2));
    CHECK(applied.find(1,{110.02,200,300},{110.02,200,1300},{0,1,0},70,1030.2,out));
    CHECK(!applied.find(1,{111,200,300},{111,200,1300},{0,1,0},70,1030.2,out));
    CHECK(!applied.find(1,{110,200,300},{110,200,1299},{0,1,0},70,1030.2,out));
    CHECK(!applied.find(1,{110,200,300},{110,200,1300},{0,1,0},71,1030.2,out));
    CHECK(!applied.find(2,{110,200,300},{110,200,1300},{0,1,0},70,1030.2,out));
    CHECK(!applied.find(1,{110,200,300},{110,200,1300},{0,1,0},70,1016.8,out));
    CHECK(!applied.find(1,{110,200,300},{110,200,1300},{0,1,0},70,1266.9,out));
    applied.clear();CHECK(!applied.find(1,{110,200,300},{110,200,1300},{0,1,0},70,1030.2,out));
    for(unsigned i=0;i<100;i++)applied.record(pose(i+1,i),{double(i),0,0},{double(i),0,1000},70,2000.+i);
    CHECK(!applied.find(1,{0,0,0},{0,0,1000},{0,1,0},70,2100,out));
    CHECK(applied.find(1,{99,0,0},{99,0,1000},{0,1,0},70,2100,out)&&near(out.position.x,99));
    // Banked views can have identical camera position/target/FOV. Their real
    // native view-up distinguishes the older queued camera from a later roll.
    applied.clear();a=pose(1,1);b=pose(2,2);b.cameraOrientation={0,0,.5f,float(std::sqrt(.75))};
    applied.record(a,{100,200,300},{100,200,1300},70,3000);
    applied.record(b,{100,200,300},{100,200,1300},70,3016);
    CHECK(applied.find(1,{100,200,300},{100,200,1300},{0,1,0},70,3020,out)&&near(out.position.x,1));
    const auto bankedUp=vehicleRotate(b.cameraOrientation,{0,1,0});
    CHECK(applied.find(1,{100,200,300},{100,200,1300},bankedUp,70,3020,out)&&near(out.position.x,2));
    CHECK(!applied.find(1,{100,200,300},{100,200,1300},{0,0,0},70,3020,out));
    // Rejection diagnostics select the closest fresh same-epoch authored
    // tuple and expose actual deltas. They never select a pose for rendering.
    applied.clear();a=pose(1,1);b=pose(2,2);
    applied.record(a,{100,200,300},{100,200,1300},70,1000.25);
    applied.record(b,{110,200,300},{110,200,1300},70,1016.9);
    auto mismatch=applied.diagnose(1,{110.2,200,300},{110.4,200,1300},{0,1,0},71,1030.2);
    CHECK(mismatch.nativeValid&&mismatch.candidate&&mismatch.candidateFresh&&mismatch.tick==2);
    CHECK(mismatch.total==2&&mismatch.sameEpoch==2&&mismatch.fresh==2);
    CHECK(near(mismatch.ageMs,13.3)&&near(mismatch.positionDelta,.2)&&near(mismatch.targetDelta,.4));
    CHECK(near(mismatch.fovDelta,1)&&near(mismatch.upDot,1)&&near(mismatch.nativeUpLength,1));
    CHECK(near(mismatch.position.x,110)&&near(mismatch.target.z,1300)&&near(mismatch.fov,70));
    auto stalePair=applied.diagnose(1,{110,200,300},{110,200,1300},{0,1,0},70,1266.9);
    CHECK(stalePair.candidate&&!stalePair.candidateFresh&&stalePair.fresh==0&&near(stalePair.ageMs,250));
    auto wrongEpoch=applied.diagnose(2,{110,200,300},{110,200,1300},{0,1,0},70,1030.2);
    CHECK(wrongEpoch.total==2&&wrongEpoch.sameEpoch==0&&wrongEpoch.fresh==0&&!wrongEpoch.candidate);
    auto invalidUp=applied.diagnose(1,{110,200,300},{110,200,1300},{0,0,0},70,1030.2);
    CHECK(!invalidUp.nativeValid&&near(invalidUp.nativeUpLength,0));
    applied.clear();
    applied.record(a,{100,200,300},{100,200,1300},70,500);
    applied.record(b,{110,200,300},{110,200,1300},70,1016.9);
    auto freshPreferred=applied.diagnose(1,{100,200,300},{100,200,1300},{0,1,0},70,1030.2);
    CHECK(freshPreferred.tick==2&&freshPreferred.fresh==1&&freshPreferred.candidateFresh&&near(freshPreferred.positionDelta,10));
    b.cameraOrientation={0,0,.5f,float(std::sqrt(.75))};applied.clear();
    applied.record(b,{110,200,300},{110,200,1300},70,1016.9);
    auto bankMismatch=applied.diagnose(1,{110,200,300},{110,200,1300},{0,1,0},70,1030.2);
    CHECK(near(bankMismatch.upDot,.5)&&near(bankMismatch.up.x,-std::sqrt(.75)));
    CHECK(!applied.find(1,{110,200,300},{110,200,1300},{0,1,0},70,1030.2,out));
    std::printf("PASS: %u checks; high-resolution source-time car interpolation, duplicate snapshots, coupled relative camera, bounded clock slew/reset/stale handling, actual capture delta, no extrapolation.\n",checks);return 0;
}

