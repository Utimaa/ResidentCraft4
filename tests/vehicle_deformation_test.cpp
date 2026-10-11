#include "../native/vehicle_deformation.hpp"
#include "../protocol/re4craft_protocol.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <chrono>

using namespace re4craft;
namespace {
size_t checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
bool near(double a,double b,double tolerance=2e-5){return std::abs(a-b)<tolerance;}
VehicleQuaternion turn(double degrees){const double angle=degrees*3.14159265358979323846/360.;return {float(std::sin(angle)),0,0,float(std::cos(angle))};}
std::vector<VehicleDeformationVertex> wheel(){std::vector<VehicleDeformationVertex> out;for(unsigned side=0;side<2;++side)for(unsigned i=0;i<24;++i){double angle=2*3.14159265358979323846*i/24;
    out.push_back({side?.05f:-.05f,float(.3*std::cos(angle)),float(.3*std::sin(angle)),0,float(std::cos(angle)),float(std::sin(angle))});}return out;}
std::vector<VehicleDeformationVertex> transformed(const std::vector<VehicleDeformationVertex>& source,VehicleQuaternion q,VehicleVector center={}){
    auto out=source;for(auto& v:out){auto p=vehicleRotate(q,deformationPosition(v)),n=vehicleRotate(q,deformationNormal(v));v={float(p.x+center.x),float(p.y+center.y),float(p.z+center.z),float(n.x),float(n.y),float(n.z)};}return out;}
struct Batch{uint32_t textureId{},flags{};std::vector<Vertex> vertices;uint32_t materialId{};std::vector<std::array<float,2>> secondaryUV;std::vector<uint32_t> sourceIndices;std::vector<uint32_t> indices;};
std::vector<Batch> topology(){Batch b{7,1,{{0,0,0,0,1,0,0,0,0xffffffff},{1,0,0,0,1,0,1,0,0xffffffff},{0,1,0,0,1,0,0,1,0xffffffff}},2,{{0,0},{1,0},{0,1}},{0,1,2}};return {b};}
void pure(){
    auto from=wheel();
    for(double angle:{-170.,-135.,-90.,-45.,1.,45.,90.,135.,170.}){
        auto to=transformed(from,turn(angle),{.04,.03,-.02});auto fit=fitVehicleDeformation(from,to);
        check(fit.safe,"observed rigid wheel rotation rejected");check(fit.maxResidual<1e-6,"rigid wheel acquired invented flex");
        for(double fraction:{0.,.1,.25,.5,.75,.9,1.})for(size_t i=0;i<from.size();i+=5){auto out=sampleVehicleDeformation(from[i],to[i],fit,fraction);
            const auto expected=vehicleRotate(turn(angle*fraction),deformationPosition(from[i]));
            check(near(out.x,expected.x+.04*fraction)&&near(out.y,expected.y+.03*fraction)&&near(out.z,expected.z-.02*fraction),"wheel radius/observed rotation/center interpolated incorrectly");
            check(near(deformationLengthSquared(deformationNormal(out)),1),"interpolated normal not normalized");}
    }
    auto opposite=transformed(from,turn(180));auto ambiguous=fitVehicleDeformation(from,opposite);
    check(!ambiguous.safe,"opposite spinning samples fabricated a rotation direction");
    auto held=sampleVehicleDeformation(from[0],opposite[0],ambiguous,.5);check(near(held.y,opposite[0].y)&&near(held.z,opposite[0].z),"unsafe wheel flattened between opposite samples");
    auto damaged=from;damaged[0].y+=.3f;check(!fitVehicleDeformation(from,damaged).safe,"large non-rigid topology shape blended");
    auto flipped=from;flipped[0].ny=-flipped[0].ny;flipped[0].nz=-flipped[0].nz;check(!fitVehicleDeformation(from,flipped).safe,"opposite source normals blended through zero");
    auto flex=transformed(from,turn(65));flex[0].x+=.003f;auto flexFit=fitVehicleDeformation(from,flex);
    check(flexFit.safe&&flexFit.maxResidual>.001,"real small residual deformation discarded");
    auto endpoint=sampleVehicleDeformation(from[0],flex[0],flexFit,1);check(std::memcmp(&endpoint,&flex[0],sizeof(endpoint))==0,"actual final endpoint changed");
    std::vector<VehicleDeformationVertex> line(3);line[0].x=-1;line[2].x=1;check(!fitVehicleDeformation(line,line).safe,"degenerate part fitted an invented rotation");
    const auto captured=turn(90);auto world=transformed(from,captured);auto local=transformed(world,{-captured.x,-captured.y,-captured.z,captured.w});
    auto canonicalFit=fitVehicleDeformation(from,local);check(canonicalFit.safe&&canonicalFit.angleRadians<1e-5,"capture rotation leaked into deformation");
    auto signLocal=transformed(world,{captured.x,captured.y,captured.z,-captured.w});
    check(fitVehicleDeformation(local,signLocal).maxResidual<1e-6,"q/-q changed captured local geometry");
    auto a=topology(),b=a;check(compatibleVehicleDeformation(a,b),"exact captured topology rejected");
    check(vehicleDeformationRepresentatives({4,0,4,3,0,3})==std::vector<uint32_t>({0,1,3}),"indexed fitting omitted or duplicated an actual palette point");
    check(vehicleDeformationWeights({4,0,4,3,0,3},{0,1,3})==std::vector<uint32_t>({2,2,2}),"indexed fitting lost expanded source weighting");
    check(vehicleDeformationRepresentatives({300000}).empty(),"unbounded source index allocated a fitting cache");
    b[0].vertices[0].x+=.03f;b[0].vertices[0].nx=.1f;check(compatibleVehicleDeformation(a,b),"actual dynamic position/normal blocked compatibility");
    auto rejects=[&](auto mutation,const char* message){auto c=a;mutation(c[0]);check(!compatibleVehicleDeformation(a,c),message);};
    rejects([](auto& v){v.textureId++;},"texture change crossed deformation");rejects([](auto& v){v.materialId++;},"material change crossed deformation");
    rejects([](auto& v){v.flags=2;},"blend/depth semantics crossed deformation");rejects([](auto& v){v.sourceIndices={1,0,2};},"changed triangle order crossed deformation");
    rejects([](auto& v){v.sourceIndices.clear();},"unknown source topology blended");rejects([](auto& v){v.vertices[0].u=.5f;},"UV0 changed through deformation");
    rejects([](auto& v){v.secondaryUV[0][0]=.5f;},"UV1 changed through deformation");rejects([](auto& v){v.vertices[0].color=0xff000000;},"source alpha/color changed through deformation");
    auto compact=a;compact[0].indices={2,0,1};compact[0].sourceIndices.clear();
    check(compatibleVehicleDeformation(compact,compact),"compact palette topology rejected");
    auto reordered=compact;reordered[0].indices={0,2,1};check(!compatibleVehicleDeformation(compact,reordered),"indexed triangle winding change crossed deformation");
    reordered=compact;reordered[0].vertices[0].u+=.25f;check(!compatibleVehicleDeformation(compact,reordered),"indexed palette UV change crossed deformation");
    check(!compatibleVehicleDeformation(a,compact),"expanded/indexed representation transition retained deformation");
    const std::vector<uint32_t> largeTopology(MaxVehicleIndices,0);
    const auto representatives=vehicleDeformationRepresentatives(largeTopology);
    check(representatives==std::vector<uint32_t>{0}&&vehicleDeformationWeights(largeTopology,representatives)==std::vector<uint32_t>{MaxVehicleIndices},"large compact topology lost exact triangle multiplicity");
    VehicleDeformationClock clock;check(clock.push(1,1000,1,1),"first capture clock");check(!clock.sample(1000).blending,"one captured frame fabricated a future endpoint");
    check(clock.push(1.1,1100,1,1),"second capture clock");auto start=clock.sample(1112);check(start.blending&&near(start.fraction,0),"wrong deformation-only reserve");
    auto middle=clock.sample(1162);check(middle.blending&&near(middle.fraction,.5),"60 Hz deformation clock not continuous");
    auto end=clock.sample(1212);check(!end.blending,"capture endpoint extrapolated");
    clock.push(1.2,1200,1,1);clock.push(1.3,1300,1,1);check(clock.count()==3,"geometry history unbounded");
    check(!clock.sample(1550).blending,"stale capture interpolated");
    check(!clock.push(1.4,1600,1,2)&&clock.count()==1,"repair identity did not reset");
    check(!clock.push(1.5,1700,2,2)&&clock.count()==1,"input epoch did not reset");
    check(!clock.push(1.4,1800,2,2)&&clock.count()==1,"source time regression crossed captures");
    check(!clock.push(2,2400,2,2)&&clock.count()==1,"large capture gap did not reset");
    clock.clear();check(clock.count()==0&&!clock.sample(1900).blending,"device/session reset retained endpoint");
    // Stable 12 Hz captures produce distinct fractions at each native 60 Hz
    // draw after the initial reserve; no physics samples are extrapolated.
    clock.push(2,2000,3,3);clock.push(2+1./12,2000+1000./12,3,3);double prior=-1;size_t different=0;
    for(unsigned i=0;i<5;++i){auto s=clock.sample(2100+i*1000./60);if(s.blending&&s.fraction>prior+1e-5)++different;prior=s.fraction;}
    check(different>=4,"deformation still stepped at capture frequency");
    clock.clear();std::vector<double> sourceTimes;double previous=-1;size_t advancing=0;
    // Irregular delivery changes the playout rate gradually; source time must
    // never jump backwards, extrapolate or alternate capture-sized steps.
    const double arrivals[]{3000,3091,3164,3261,3333,3422,3504};
    size_t next=0;
    for(double now=3000;now<3540;now+=1000./60){while(next<7&&arrivals[next]<=now){clock.push(3+next/12.,arrivals[next],4,4);sourceTimes.push_back(3000+next*1000./12);if(sourceTimes.size()>3)sourceTimes.erase(sourceTimes.begin());++next;}
        const auto s=clock.sample(now);if(s.blending){const double actual=sourceTimes[s.from]+(sourceTimes[s.to]-sourceTimes[s.from])*s.fraction;
            check(actual>=previous-1e-6&&actual<=sourceTimes.back()+1e-6,"jitter caused rewind/extrapolation");
            if(previous>=0&&actual>previous+.1)++advancing;previous=actual;}}
    check(advancing>=20,"irregular delivery kept the geometry at source frame rate");
}
struct Captured{Mesh mesh{};VehicleMeshCapture capture{};std::vector<Batch> batches;std::vector<std::vector<VehicleDeformationVertex>> local;};
template<class T> void read(std::ifstream& in,T& v){in.read(reinterpret_cast<char*>(&v),sizeof(v));check(bool(in),"truncated actual source fixture");}
Captured captured(const char* path){std::ifstream in(path,std::ios::binary);check(bool(in),"open actual surface body");Captured c;read(in,c.mesh);read(in,c.capture);
    check(c.mesh.category==1&&c.mesh.batchCount<=MaxVehicleBatches,"actual surface metadata bounds");VehicleQuaternion q{c.capture.quaternion[0],c.capture.quaternion[1],c.capture.quaternion[2],c.capture.quaternion[3]};q=normalizedVehicleQuaternion(q);q={-q.x,-q.y,-q.z,q.w};
    size_t expanded{},unique{};for(uint32_t i=0;i<c.mesh.batchCount;++i){IndexedVehicleBatch h{};read(in,h);check(h.indexCount%3==0&&h.indexCount<=MaxVehicleIndices-expanded&&h.vertexCount<=h.indexCount&&h.vertexCount<=MaxVehicleVertices-unique,"actual indexed geometry bounds");expanded+=h.indexCount;unique+=h.vertexCount;
        std::vector<VehicleVertex> palette(h.vertexCount);in.read(reinterpret_cast<char*>(palette.data()),palette.size()*sizeof(VehicleVertex));check(bool(in),"read actual vertex palette");
        Batch b{h.textureId,h.flags,{},h.materialId};b.sourceIndices.resize(h.indexCount);in.read(reinterpret_cast<char*>(b.sourceIndices.data()),b.sourceIndices.size()*4);check(bool(in),"read actual index sequence");
        std::vector<VehicleDeformationVertex> local;local.reserve(h.indexCount);for(auto index:b.sourceIndices){check(index<palette.size(),"actual source index bound");const auto& v=palette[index];
            b.vertices.push_back({v.x,v.y,v.z,v.nx,v.ny,v.nz,v.u,v.v,v.color});b.secondaryUV.push_back({v.u2,v.v2});auto p=vehicleRotate(q,{v.x,v.y,v.z}),n=vehicleRotate(q,{v.nx,v.ny,v.nz});
            local.push_back({float(p.x),float(p.y),float(p.z),float(n.x),float(n.y),float(n.z)});}
        c.batches.push_back(std::move(b));c.local.push_back(std::move(local));}
    check(in.peek()==std::char_traits<char>::eof(),"extra source body bytes");return c;}
void actual(const char* first,const char* second){auto from=captured(first),to=captured(second);check(compatibleVehicleDeformation(from.batches,to.batches),"actual Covet topology/material/UV/index changed");
    std::vector<std::vector<uint32_t>> representatives,weights;size_t unique{};for(const auto& batch:from.batches){representatives.push_back(vehicleDeformationRepresentatives(batch.sourceIndices));weights.push_back(vehicleDeformationWeights(batch.sourceIndices,representatives.back()));unique+=representatives.back().size();}
    std::vector<double> times;size_t safe{},rotating{},vertices{};double residual{},allSourceResidual{},rotationDifference{};
    for(unsigned run=0;run<20;++run){auto begin=std::chrono::steady_clock::now();size_t accepted=0,turned=0;for(size_t i=0;i<from.local.size();++i){auto fit=fitVehicleDeformation(VehicleDeformationUniqueView{from.local[i],representatives[i],&weights[i]},VehicleDeformationUniqueView{to.local[i],representatives[i],&weights[i]});accepted+=fit.safe;turned+=fit.safe&&fit.angleRadians>.05;
            if(!run){vertices+=from.local[i].size();residual=std::max(residual,fit.maxResidual);}}
        times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());safe=accepted;rotating=turned;}
    // Independently walk every full-resolution expanded point: the cheaper fit
    // must still cover all real geometry and preserve all endpoint bytes.
    for(size_t i=0;i<from.local.size();++i){const auto fit=fitVehicleDeformation(VehicleDeformationUniqueView{from.local[i],representatives[i],&weights[i]},VehicleDeformationUniqueView{to.local[i],representatives[i],&weights[i]});
        const auto expandedFit=fitVehicleDeformation(from.local[i],to.local[i]);const auto delta=vehicleQuaternionDelta(fit.rotation,expandedFit.rotation);
        check(near(fit.fromCenter.x,expandedFit.fromCenter.x,1e-9)&&near(fit.fromCenter.y,expandedFit.fromCenter.y,1e-9)&&near(fit.fromCenter.z,expandedFit.fromCenter.z,1e-9)&&
            near(fit.maxResidual,expandedFit.maxResidual,1e-6),"unique weighting changed the full-source fitted transform");
        rotationDifference=std::max(rotationDifference,2*std::acos(std::clamp(std::abs(double(delta.w)),0.,1.))*180/3.14159265358979323846);
        double maximum{};for(size_t n=0;n<from.local[i].size();++n){const auto rotated=vehicleRotate(fit.rotation,deformationSubtract(deformationPosition(from.local[i][n]),fit.fromCenter));
            maximum=std::max(maximum,std::sqrt(deformationLengthSquared(deformationSubtract(rotated,deformationSubtract(deformationPosition(to.local[i][n]),fit.toCenter)))));
            const auto endpoint=sampleVehicleDeformation(from.local[i][n],to.local[i][n],fit,1);check(std::memcmp(&endpoint,&to.local[i][n],sizeof(endpoint))==0,"indexed fit changed a full-resolution actual endpoint");}
        check(near(maximum,fit.maxResidual,1e-7),"indexed fit missed actual source deformation");allSourceResidual=std::max(allSourceResidual,maximum);}
    std::sort(times.begin(),times.end());check(safe&&vertices>200000,"full actual car fitting not exercised");
    std::printf("Actual source geometry: %zu expanded/%zu unique vertices/%zu batches, %zu safe observed fits/%zu rotating, capture delta%.3fms; unique fit CPU median%.3fms p95%.3fms, max residual%.6fm/all-source%.6fm; rotation difference vs expanded weighting<=%.5fdeg. Fits occur once per received mesh, not per native draw.\n",vertices,unique,from.batches.size(),safe,rotating,(to.capture.sourceSeconds-from.capture.sourceSeconds)*1000,times[10],times[18],residual,allSourceResidual,rotationDifference);
}
}
int main(int argc,char** argv){try{pure();if(argc==3)actual(argv[1],argv[2]);else if(argc!=1)throw std::runtime_error("usage: vehicle_deformation_test [actual-from-surface-body.bin actual-to-surface-body.bin]");
    std::printf("PASS: %zu deformation geometry/timing/topology checks.\n",checks);return 0;}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s after%zu checks\n",e.what(),checks);return 1;}}
