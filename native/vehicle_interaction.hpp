#pragma once
#include "game_access.hpp"
#include "../protocol/re4craft_protocol.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace re4craft {
// Motion retains the v6 velocity unit: metres per 1/20 second. BeamNG's
// engine velocities are metres/second and must be divided by20 by its producer.
inline double vehicleSpeedMetresPerSecond(const GuestMotion& motion) {
    return 20.*std::sqrt(double(motion.velocityX)*motion.velocityX+double(motion.velocityY)*motion.velocityY+double(motion.velocityZ)*motion.velocityZ);
}
inline float vehicleImpactDamage(double speed){return static_cast<float>(std::min(40.,.025*speed*speed));}
inline float vehicleImpactShove(double speed){return static_cast<float>(std::min(2.,.08*speed));}
inline bool vehicleKnockdownImpact(const GuestMelee& hit) {
    // Ten miles/hour in the actual BeamNG metres/second unit. The producer
    // encodes this measured speed through the existing quadratic damage and
    // linear shove fields; no extra Minecraft flag or synthetic speed is used.
    return hit.flags==HitVehicle&&std::isfinite(hit.damage)&&std::isfinite(hit.knockback)&&
        hit.damage>=vehicleImpactDamage(4.4704)&&hit.knockback>=vehicleImpactShove(4.4704)-.001f;
}
inline bool vehicleImpactGeometryInReach(const GuestMelee& hit,const Enemy& actor,const GuestMotion& motion) {
    if(hit.flags!=HitVehicle||hit.epoch!=motion.epoch||!hit.eventId||hit.targetId!=actor.id||!actor.id||
       !(actor.flags&ActorAlive)||(actor.flags&ActorDead)||(!(actor.flags&ActorHostile)&&!(actor.flags&ActorBreakable))||
       (actor.flags&(ActorHostile|ActorBreakable))==(ActorHostile|ActorBreakable)||
       (motion.flags&(MotionReady|MotionCameraReady))!=(MotionReady|MotionCameraReady)||(motion.flags&MotionDead))return false;
    for(double x:{hit.attackX,hit.attackY,hit.attackZ,motion.hostX,motion.hostY,motion.hostZ,actor.x,actor.y,actor.z})if(!std::isfinite(x)||std::abs(x)>=100000.)return false;
    for(float x:{hit.damage,hit.knockback,hit.directionX,hit.directionY,hit.directionZ,hit.maxReach,motion.velocityX,motion.velocityY,motion.velocityZ,
                 actor.minX,actor.minY,actor.minZ,actor.maxX,actor.maxY,actor.maxZ})if(!std::isfinite(x))return false;
    if(actor.minX>actor.maxX||actor.minY>actor.maxY||actor.minZ>actor.maxZ||hit.maxReach<=0||hit.maxReach>1||
       hit.damage<=0||hit.damage>40||hit.knockback<0||hit.knockback>2)return false;
    double directionLength=double(hit.directionX)*hit.directionX+double(hit.directionY)*hit.directionY+double(hit.directionZ)*hit.directionZ;
    if(directionLength<.9*.9||directionLength>1.1*1.1)return false;
    const double ox=hit.attackX-motion.hostX,oy=hit.attackY-motion.hostY,oz=hit.attackZ-motion.hostZ;
    if(ox*ox+oy*oy+oz*oz>64.)return false;
    double x=std::clamp(hit.attackX,actor.x+actor.minX,actor.x+actor.maxX)-hit.attackX;
    double y=std::clamp(hit.attackY,actor.y+actor.minY,actor.y+actor.maxY)-hit.attackY;
    double z=std::clamp(hit.attackZ,actor.z+actor.minZ,actor.z+actor.maxZ)-hit.attackZ;
    return x*x+y*y+z*z<=double(hit.maxReach)*hit.maxReach;
}
inline bool vehicleImpactInReach(const GuestMelee& hit,const Enemy& actor,const GuestMotion& motion) {
    if(!vehicleImpactGeometryInReach(hit,actor,motion))return false;
    const double speed=vehicleSpeedMetresPerSecond(motion);
    if(!std::isfinite(speed)||speed<.5||speed>128.*20.*std::sqrt(3.))return false;
    // Up to2m/s accounts for bounded publication/callback skew. A contact
    // must still match actual recent pre-impact motion, even when the native
    // transport receives its stopped post-impact snapshot first.
    if(hit.damage>vehicleImpactDamage(speed+2.)+.001f||hit.knockback>vehicleImpactShove(speed+2.)+.001f)return false;
    double alignment=(double(hit.directionX)*motion.velocityX+double(hit.directionY)*motion.velocityY+double(hit.directionZ)*motion.velocityZ)/(speed/20.);
    return alignment>=.5;
}
class VehicleMotionHistory {
    struct Entry {GuestMotion motion{};uint64_t time{};};
    std::array<Entry,32> samples_{};
    size_t next_{},count_{};
public:
    void clear(){next_=count_=0;}
    void push(const GuestMotion& motion,uint64_t now){samples_[next_]={motion,now};next_=(next_+1)%samples_.size();count_=std::min(count_+1,samples_.size());}
    bool match(const GuestMelee& hit,const Enemy& actor,const GuestMotion& current,uint64_t now,GuestMotion& witness) const {
        if(!vehicleImpactGeometryInReach(hit,actor,current))return false;
        for(size_t i=0;i<count_;++i){
            const auto& sample=samples_[(next_+samples_.size()-1-i)%samples_.size()];
            if(now<sample.time||now-sample.time>300||sample.motion.epoch!=current.epoch)continue;
            if(vehicleImpactInReach(hit,actor,sample.motion)){witness=sample.motion;return true;}
        }
        return false;
    }
};
inline Vec3 vehicleShoveMetres(const GuestMelee& hit) {
    double length=std::hypot(double(hit.directionX),double(hit.directionZ));
    if(hit.flags!=HitVehicle||!std::isfinite(length)||length<.1||!std::isfinite(hit.knockback)||hit.knockback<0||hit.knockback>2)return {};
    return {static_cast<float>(hit.directionX/length*hit.knockback),0.f,static_cast<float>(hit.directionZ/length*hit.knockback)};
}
// Contact callback storms cannot translate/damage the same actor every node or
// rendering frame. IDs are monotonic across the complete active epoch.
class VehicleContactLedger {
    struct Entry {uint32_t id;uint64_t time;};
    uint32_t epoch_{},lastEvent_{};
    std::vector<Entry> entries_;
public:
    void clear(){epoch_=lastEvent_=0;entries_.clear();}
    bool accept(uint32_t epoch,uint32_t id,uint32_t event,uint64_t now) {
        if(!id||!event||!epoch)return false;
        if(epoch!=epoch_){epoch_=epoch;lastEvent_=0;entries_.clear();}
        if(event<=lastEvent_)return false;
        for(auto& e:entries_)if(e.id==id){if(now<e.time||now-e.time<250)return false;e.time=now;lastEvent_=event;return true;}
        entries_.erase(std::remove_if(entries_.begin(),entries_.end(),[&](const Entry& e){return now>=e.time&&now-e.time>=2000;}),entries_.end());
        if(entries_.size()>=64)return false;
        entries_.push_back({id,now});lastEvent_=event;return true;
    }
};
}
