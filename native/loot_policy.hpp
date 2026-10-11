#pragma once
#include "../protocol/re4craft_protocol.h"
#include <algorithm>
#include <vector>
#include <cstdint>
#include <cmath>

namespace re4craft {
// GLOBAL.chapter_4F9A is the current zero-based subchapter: the actual native
// chapter-title table maps 0 to 1-1 and 18 to 6-1. End-of-chapter routines
// advance that counter; their index+1 write is not the active chapter's base.
// Leon is also playable in Mercenaries, so character alone is insufficient.
// These verified Flags_SYSTEM_0_54 bits select Assignment Ada, Mercenaries
// and Separate Ways. Other native system flags do not change loot progression.
constexpr uint32_t NativeBonusGameMask=0xc0001000U;
constexpr int nativeLootTier(uint8_t chapter,uint8_t character,uint32_t systemFlags) {
    return character||(systemFlags&NativeBonusGameMask)||chapter>18?-1:
        chapter<=2?0:chapter<=5?1:chapter<=9?2:chapter<=13?3:chapter<=17?4:5;
}
constexpr bool nativeOrdinaryLootItem(uint32_t id) {
    switch(id){
    case 0:case 1:case 2:case 4:case 5:case 6:case 7:case 8:case 9:case 10:
    case 14:case 17:case 18:case 19:case 20:case 21:case 22:case 24:case 25:
    case 26:case 28:case 32:case 70:case 106:case 114:case 120:case 121:case 168:return true;
    default:return false;
    }
}
constexpr bool nativeLootItemEligible(uint32_t id,uint32_t count,uint16_t itemFlag,uint16_t autoItemFlag) {
    return nativeOrdinaryLootItem(id)&&count>0&&count<=65535&&!itemFlag&&!autoItemFlag;
}
class NativeLootQueue {
    uint64_t session_{};uint32_t next_{};std::vector<HostLoot> pending_;
public:
    void initialize(uint64_t session){if(!session_&&session)session_=session;}
    bool append(uint32_t epoch,uint32_t source,uint32_t tier,double x,double y,double z,HostLoot& accepted){
        if(!session_||!epoch||source<1||source>2||tier>5||next_==UINT32_MAX||pending_.size()>=128)return false;
        for(double value:{x,y,z})if(!std::isfinite(value)||std::abs(value)>30000000)return false;
        accepted={epoch,++next_,source,tier,session_,x,y,z};pending_.push_back(accepted);return true;
    }
    std::vector<HostLoot> take(){std::vector<HostLoot> result;result.swap(pending_);return result;}
    bool retry(const HostLoot& loot){
        if(loot.sessionId!=session_||!loot.eventId||loot.eventId>next_||pending_.size()>=256)return false;
        auto at=std::lower_bound(pending_.begin(),pending_.end(),loot.eventId,[](const HostLoot& a,uint32_t id){return a.eventId<id;});
        if(at!=pending_.end()&&at->eventId==loot.eventId)return true;
        pending_.insert(at,loot);return true;
    }
};
}
