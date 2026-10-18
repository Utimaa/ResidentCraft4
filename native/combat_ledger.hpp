#pragma once
#include "game_access.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace re4craft {
// Verified native consumer weapon IDs: knife10hex, explosion12hex,
// damaging fire13hex. Ignition17hex has zero native base damage and must not
// substitute for actual Minecraft burn damage. These are dispatcher IDs.
inline bool guestNativeDamageWeapon(uint32_t weapon){return weapon==0x01||weapon==0x10||weapon==0x12||weapon==0x13;}
inline uint32_t guestNativeDamageWeaponForFlags(uint32_t flags){return (flags&16U)?0x12U:(flags&4U)?0x13U:(flags&2U)?0x01U:0x10U;}
struct NativeDamageIdentity {
    uint32_t weapon{},yarare{};
    Vec3 source{};
};
enum class DamageClaim { Unrelated, Apply, AlreadyApplied };
enum class DamageConsumer { Life, WeaponPower };
// A damage-info fingerprint prevents a queued hit from changing unrelated
// native attacks or a recycled enemy allocation. This class never writes game memory.
class CombatDamageLedger {
    struct Entry {
        uint32_t epoch,id,event;
        uintptr_t model;
        NativeDamageIdentity info;
        int damage;
        uint64_t expires;
        bool applied;
        DamageConsumer consumer;
    };
    std::vector<Entry> entries_;
    static bool equal(const NativeDamageIdentity& a,const NativeDamageIdentity& b){
        return guestNativeDamageWeapon(a.weapon)&&a.weapon==b.weapon&&a.yarare==b.yarare&&a.source.x==b.source.x&&a.source.y==b.source.y&&a.source.z==b.source.z;
    }
public:
    void clear(){entries_.clear();}
    void expire(uint64_t now){entries_.erase(std::remove_if(entries_.begin(),entries_.end(),[&](const Entry& e){return now>e.expires;}),entries_.end());}
    bool pending(uint32_t epoch,uint32_t id,uint64_t now){expire(now);return std::any_of(entries_.begin(),entries_.end(),[&](const Entry& e){return e.epoch==epoch&&e.id==id&&!e.applied;});}
    bool arm(uint32_t epoch,uint32_t id,uint32_t event,uintptr_t model,NativeDamageIdentity info,int damage,uint64_t now,DamageConsumer consumer=DamageConsumer::Life){
        expire(now);
        if(!id||!event||!model||!info.yarare||!guestNativeDamageWeapon(info.weapon)||damage<=0||damage>32767||!std::isfinite(info.source.x)||!std::isfinite(info.source.y)||!std::isfinite(info.source.z))return false;
        for(auto& e:entries_)if(e.epoch==epoch&&e.id==id){if(!e.applied||event<=e.event)return false;e={epoch,id,event,model,info,damage,now+300,false,consumer};return true;}
        if(entries_.size()>=64){
            auto retired=std::find_if(entries_.begin(),entries_.end(),[](const Entry& e){return e.applied;});
            if(retired==entries_.end())return false;entries_.erase(retired);
        }
        entries_.push_back({epoch,id,event,model,info,damage,now+300,false,consumer});return true;
    }
    DamageClaim consume(uint32_t epoch,uint32_t id,uintptr_t model,NativeDamageIdentity info,uint64_t now,int& damage,DamageConsumer consumer=DamageConsumer::Life){
        expire(now);
        for(auto& e:entries_)if(e.epoch==epoch&&e.id==id&&e.model==model&&e.consumer==consumer&&equal(e.info,info)){
            damage=e.damage;
            if(e.applied)return DamageClaim::AlreadyApplied;
            e.applied=true;damage=e.damage;return DamageClaim::Apply;
        }
        return DamageClaim::Unrelated;
    }
};
inline bool nativeGanadoKind(uint32_t id){return id!=0x4b&&id!=0x4e&&((id>=0x40&&id<=0x4f)||(id>=0x10&&id<=0x20));}
}
