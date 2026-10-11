#pragma once
#include <cstdint>
#include <cstddef>

namespace re4craft {
// re4_tweaks SDK cModel::m_DmgInfo_328 + cDmgInfo::m_Timer_5.
// Trainer.cpp's original invincibility handling identifies this nonzero timer
// as the common enemy Catch gate. Each scoped owner borrows one byte only
// during the verified native scheduler call, with exact actor restoration.
class ScopedVehicleGrabProtection {
    uintptr_t player_{};
    uint32_t identity_{};
    uint8_t previous_{};
    unsigned depth_{};
    bool owned_{};
public:
    static constexpr uintptr_t IdentityOffset=0xf8,TimerOffset=0x32d;
    static constexpr uint8_t ProtectedTimer=128;
    bool ownsProtection()const{return owned_;}
    template<class Read,class Write>
    bool begin(bool eligible,uintptr_t player,Read read,Write write) {
        if(depth_){++depth_;return owned_;}
        depth_=1;
        if(!eligible||player<0x10000||player>UINT32_MAX-TimerOffset)return false;
        uint32_t id{};uint8_t timer{};
        if(!read(player+IdentityOffset,&id,sizeof(id))||
           !read(player+TimerOffset,&timer,sizeof(timer)))return false;
        const uint8_t protectedTimer=ProtectedTimer;
        if(!write(player+TimerOffset,&protectedTimer,sizeof(protectedTimer)))return false;
        player_=player;identity_=id;previous_=timer;owned_=true;
        return true;
    }
    template<class Read,class Write>
    bool end(uintptr_t currentPlayer,Read read,Write write) {
        if(!depth_)return false;
        if(--depth_)return false;
        bool restored=false;
        if(owned_&&currentPlayer==player_){
            uint32_t id{};
            if(read(player_+IdentityOffset,&id,sizeof(id))&&id==identity_)
                restored=write(player_+TimerOffset,&previous_,sizeof(previous_));
        }
        owned_=false;player_=0;identity_=0;previous_=0;
        return restored;
    }
};
}
