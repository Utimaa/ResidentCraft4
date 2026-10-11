#pragma once
#include "vehicle_presentation.hpp"
#include "../protocol/re4craft_protocol.h"
#include <vector>
#include <array>
#include <limits>

namespace re4craft {
// Visual geometry only. Every endpoint is an actually captured GPU vertex;
// none of these values is used for vehicle physics, contacts or readiness.
struct VehicleDeformationVertex {float x{},y{},z{},nx{},ny{},nz{};};
inline VehicleVector deformationPosition(const VehicleDeformationVertex& v){return {v.x,v.y,v.z};}
inline VehicleVector deformationNormal(const VehicleDeformationVertex& v){return {v.nx,v.ny,v.nz};}
inline VehicleVector deformationSubtract(VehicleVector a,VehicleVector b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline double deformationLengthSquared(VehicleVector a){return a.x*a.x+a.y*a.y+a.z*a.z;}
struct VehicleDeformationFit {
    VehicleVector fromCenter{},toCenter{};
    VehicleQuaternion rotation{};
    double radius{},maxResidual{},angleRadians{};
    bool safe{};
};
struct VehicleDeformationUniqueView {
    const std::vector<VehicleDeformationVertex>& vertices;const std::vector<uint32_t>& representatives;const std::vector<uint32_t>* weights{};
    size_t size()const{return representatives.size();}
    const VehicleDeformationVertex& operator[](size_t i)const{return vertices[representatives[i]];}
};
template<class Vertices> inline double vehicleDeformationWeight(const Vertices&,size_t){return 1.;}
inline double vehicleDeformationWeight(const VehicleDeformationUniqueView& view,size_t i){return view.weights?double((*view.weights)[i]):1.;}
inline std::vector<uint32_t> vehicleDeformationRepresentatives(const std::vector<uint32_t>& indices){
    std::vector<uint32_t> out;if(indices.empty())return out;
    // Surface13 has already strictly bounded and expanded this palette. Each
    // repeated index refers to identical position/normal bytes in its frame.
    const uint32_t largest=*std::max_element(indices.begin(),indices.end());if(largest>=MaxVehicleVertices||indices.size()>MaxVehicleIndices)return out;
    std::vector<uint8_t> seen(size_t(largest)+1);out.reserve(std::min(indices.size(),seen.size()));
    for(size_t i=0;i<indices.size();++i)if(!seen[indices[i]]){seen[indices[i]]=1;out.push_back(uint32_t(i));}
    return out;
}
inline std::vector<uint32_t> vehicleDeformationWeights(const std::vector<uint32_t>& indices,const std::vector<uint32_t>& representatives){
    std::vector<uint32_t> out;if(indices.empty()||representatives.empty())return out;
    const uint32_t largest=*std::max_element(indices.begin(),indices.end());if(largest>=MaxVehicleVertices||indices.size()>MaxVehicleIndices)return out;
    std::vector<uint32_t> counts(size_t(largest)+1);for(auto index:indices)++counts[index];
    out.reserve(representatives.size());for(auto offset:representatives){if(offset>=indices.size())return {};out.push_back(counts[indices[offset]]);}return out;
}
template<class Vertices> inline VehicleDeformationFit fitVehicleDeformation(const Vertices& from,const Vertices& to){
    VehicleDeformationFit fit{};if(from.size()!=to.size()||from.size()<3)return fit;
    double totalWeight{};
    for(size_t i=0;i<from.size();++i){auto a=deformationPosition(from[i]),b=deformationPosition(to[i]);const double weight=vehicleDeformationWeight(from,i);totalWeight+=weight;
        fit.fromCenter.x+=a.x*weight;fit.fromCenter.y+=a.y*weight;fit.fromCenter.z+=a.z*weight;
        fit.toCenter.x+=b.x*weight;fit.toCenter.y+=b.y*weight;fit.toCenter.z+=b.z*weight;}
    if(totalWeight<=0)return fit;const double inverseCount=1./totalWeight;
    for(double* value:{&fit.fromCenter.x,&fit.fromCenter.y,&fit.fromCenter.z,&fit.toCenter.x,&fit.toCenter.y,&fit.toCenter.z})*value*=inverseCount;
    double s[3][3]{};
    for(size_t i=0;i<from.size();++i){const auto a=deformationSubtract(deformationPosition(from[i]),fit.fromCenter),b=deformationSubtract(deformationPosition(to[i]),fit.toCenter);
        const double av[3]{a.x,a.y,a.z},bv[3]{b.x,b.y,b.z};
        fit.radius=std::max(fit.radius,std::sqrt(deformationLengthSquared(a)));
        const double weight=vehicleDeformationWeight(from,i);
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)s[r][c]+=av[r]*bv[c]*weight;}
    // Horn's symmetric quaternion eigenproblem, solved with bounded Jacobi
    // sweeps. Unlike vertex lerp, an observed spinning wheel keeps its radius.
    double a[4][4]{{s[0][0]+s[1][1]+s[2][2],s[1][2]-s[2][1],s[2][0]-s[0][2],s[0][1]-s[1][0]},
        {s[1][2]-s[2][1],s[0][0]-s[1][1]-s[2][2],s[0][1]+s[1][0],s[0][2]+s[2][0]},
        {s[2][0]-s[0][2],s[0][1]+s[1][0],-s[0][0]+s[1][1]-s[2][2],s[1][2]+s[2][1]},
        {s[0][1]-s[1][0],s[0][2]+s[2][0],s[1][2]+s[2][1],-s[0][0]-s[1][1]+s[2][2]}};
    double eigenvectors[4][4]{};for(unsigned i=0;i<4;++i)eigenvectors[i][i]=1;
    for(unsigned sweep=0;sweep<16;++sweep)for(unsigned p=0;p<4;++p)for(unsigned q=p+1;q<4;++q){
        if(std::abs(a[p][q])<=1e-15*(1+std::abs(a[p][p])+std::abs(a[q][q])))continue;
        const double angle=.5*std::atan2(2*a[p][q],a[q][q]-a[p][p]),c=std::cos(angle),sn=std::sin(angle);
        const double app=a[p][p],aqq=a[q][q],apq=a[p][q];
        a[p][p]=c*c*app-2*c*sn*apq+sn*sn*aqq;a[q][q]=sn*sn*app+2*c*sn*apq+c*c*aqq;a[p][q]=a[q][p]=0;
        for(unsigned k=0;k<4;++k){if(k!=p&&k!=q){const double kp=a[k][p],kq=a[k][q];a[k][p]=a[p][k]=c*kp-sn*kq;a[k][q]=a[q][k]=sn*kp+c*kq;}
            const double vp=eigenvectors[k][p],vq=eigenvectors[k][q];eigenvectors[k][p]=c*vp-sn*vq;eigenvectors[k][q]=sn*vp+c*vq;}}
    unsigned largest=0,second=1;for(unsigned i=1;i<4;++i)if(a[i][i]>a[largest][largest])largest=i;
    second=largest==0?1:0;for(unsigned i=0;i<4;++i)if(i!=largest&&a[i][i]>a[second][second])second=i;
    if(fit.radius<1e-5||a[largest][largest]-a[second][second]<1e-9*std::max(1.,std::abs(a[largest][largest])))return fit;
    const double sign=eigenvectors[0][largest]<0?-1.:1.;
    fit.rotation=normalizedVehicleQuaternion({float(sign*eigenvectors[1][largest]),float(sign*eigenvectors[2][largest]),float(sign*eigenvectors[3][largest]),float(sign*eigenvectors[0][largest])});
    fit.angleRadians=2*std::acos(std::clamp(double(fit.rotation.w),0.,1.));
    bool normalsCoherent=true;
    for(size_t i=0;i<from.size();++i){const auto rotated=vehicleRotate(fit.rotation,deformationSubtract(deformationPosition(from[i]),fit.fromCenter));
        fit.maxResidual=std::max(fit.maxResidual,std::sqrt(deformationLengthSquared(deformationSubtract(rotated,deformationSubtract(deformationPosition(to[i]),fit.toCenter)))));
        const auto normal=vehicleRotate(fit.rotation,deformationNormal(from[i])),next=deformationNormal(to[i]);
        if(deformationLengthSquared(normal)>1e-12&&deformationLengthSquared(next)>1e-12&&normal.x*next.x+normal.y*next.y+normal.z*next.z<0)normalsCoherent=false;}
    // At an almost-opposite sample the real spin direction is unknowable.
    // Large deformation/teleport is also not interpolated across unrelated
    // surfaces. Display the actual endpoint instead of collapsing the part.
    constexpr double radians=3.14159265358979323846/180.;
    fit.safe=normalsCoherent&&std::isfinite(fit.maxResidual)&&fit.angleRadians<175*radians&&
        fit.maxResidual<=std::max(.015,fit.radius*.08)&&
        deformationLengthSquared(deformationSubtract(fit.toCenter,fit.fromCenter))<=4.;
    return fit;
}
inline VehicleDeformationVertex sampleVehicleDeformation(const VehicleDeformationVertex& a,const VehicleDeformationVertex& b,const VehicleDeformationFit& fit,double fraction){
    if(!fit.safe||fraction>=1)return b;if(fraction<=0)return a;
    const auto q=vehicleSlerp({},fit.rotation,fraction),inverse=VehicleQuaternion{-fit.rotation.x,-fit.rotation.y,-fit.rotation.z,fit.rotation.w};
    const auto shape=vehicleLerp(deformationSubtract(deformationPosition(a),fit.fromCenter),vehicleRotate(inverse,deformationSubtract(deformationPosition(b),fit.toCenter)),fraction);
    const auto center=vehicleLerp(fit.fromCenter,fit.toCenter,fraction),rotated=vehicleRotate(q,shape);
    auto normal=vehicleRotate(q,vehicleLerp(deformationNormal(a),vehicleRotate(inverse,deformationNormal(b)),fraction));
    const double length=std::sqrt(deformationLengthSquared(normal));if(length>1e-8){normal.x/=length;normal.y/=length;normal.z/=length;}
    else normal=deformationNormal(b);
    return {float(center.x+rotated.x),float(center.y+rotated.y),float(center.z+rotated.z),float(normal.x),float(normal.y),float(normal.z)};
}
// Three complete endpoints bound CPU memory. The playout reserve applies only
// to flex/props; the independently latched rigid body and camera remain fresh.
class VehicleDeformationClock {
public:
    struct Selection {size_t from{},to{};double fraction{1};bool blending{};};
    static constexpr double MaxGapMs=250,MinDelayMs=75,MaxDelayMs=125;
    void clear(){count_=0;started_=false;delay_=MinDelayMs;interval_=0;rate_=1;}
    bool push(double seconds,double arrival,uint32_t epoch,uint32_t vehicleId){
        if(!epoch||!vehicleId||!std::isfinite(seconds)||seconds<0||!std::isfinite(arrival)||arrival<0)return false;
        bool reset=false;
        if(count_){const auto& last=history_[count_-1];reset=epoch!=epoch_||vehicleId!=vehicleId_||seconds<=last.seconds||arrival<last.arrival||
            (seconds-last.seconds)*1000>MaxGapMs||arrival-last.arrival>MaxGapMs;
            if(!reset){const double observed=(seconds-last.seconds)*1000.;interval_=interval_?interval_+.15*(observed-interval_):observed;
                delay_=std::clamp(interval_+12,MinDelayMs,MaxDelayMs);}}
        if(reset)clear();
        if(!count_){epoch_=epoch;vehicleId_=vehicleId;offset_=arrival-seconds*1000;}
        offset_=std::min(offset_,arrival-seconds*1000);
        if(count_==history_.size()){std::move(history_.begin()+1,history_.end(),history_.begin());--count_;}
        history_[count_++]={seconds,arrival};return !reset;
    }
    Selection sample(double now){
        if(!count_)return {};Selection out{count_-1,count_-1,1,false};
        if(!std::isfinite(now)||now<history_[count_-1].arrival||now-history_[count_-1].arrival>=MaxGapMs){started_=false;return out;}
        if(count_<2){started_=false;return out;}
        const double desired=now-offset_-delay_;
        if(!started_){playout_=desired;started_=true;}else if(now>=lastNow_){const double elapsed=now-lastNow_,error=desired-(playout_+elapsed);
            const double targetRate=std::clamp(1.+error/200.,.85,1.15);
            rate_+=std::clamp(targetRate-rate_,-elapsed*.0015,elapsed*.0015);
            playout_=std::max(playout_,std::min(history_[count_-1].seconds*1000,playout_+elapsed*rate_));}
        lastNow_=now;
        if(playout_>=history_[count_-1].seconds*1000)return out;
        for(size_t i=1;i<count_;++i)if(playout_<=history_[i].seconds*1000){
            const double left=history_[i-1].seconds*1000,right=history_[i].seconds*1000;
            return {i-1,i,std::clamp((playout_-left)/(right-left),0.,1.),true};}
        return out;
    }
    size_t count()const{return count_;}double delayMs()const{return delay_;}
private:
    struct Stamp{double seconds{},arrival{};};std::array<Stamp,3> history_{};size_t count_{};uint32_t epoch_{},vehicleId_{};
    double offset_{},delay_{MinDelayMs},interval_{},rate_{1},playout_{},lastNow_{};bool started_{};
};
template<class Batch> bool compatibleVehicleDeformation(const std::vector<Batch>& a,const std::vector<Batch>& b){
    if(a.size()!=b.size()||a.empty())return false;
    for(size_t i=0;i<a.size();++i){const auto& left=a[i];const auto& right=b[i];
        if(left.textureId!=right.textureId||left.materialId!=right.materialId||left.flags!=right.flags||left.vertices.size()!=right.vertices.size()||
           left.indices!=right.indices||
           (left.indices.empty()&&(left.sourceIndices.empty()||left.sourceIndices!=right.sourceIndices||left.sourceIndices.size()!=left.vertices.size()))||
           left.secondaryUV!=right.secondaryUV)return false;
        for(size_t n=0;n<left.vertices.size();++n){const auto& x=left.vertices[n];const auto& y=right.vertices[n];if(x.u!=y.u||x.v!=y.v||x.color!=y.color)return false;}}
    return true;
}
}
