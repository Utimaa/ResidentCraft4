#pragma once
#include "game_access.hpp"
#include <array>
#include <cmath>
#include <cstring>

namespace re4craft {
inline bool sameQueuedWorldView(const NativeSnapshot& a,const NativeSnapshot& b) {
    // Repeated per-object projection commands are still the same native world
    // pass only when their enqueue-time camera and allocation match exactly.
    return a.globalAddress==b.globalAddress&&a.room==b.room
        &&std::memcmp(a.view,b.view,sizeof(a.view))==0
        &&std::memcmp(a.projection,b.projection,sizeof(a.projection))==0;
}
inline bool sameWorldProjection(const float (&matrix)[4][4],const NativeSnapshot& native) {
    for(int r=0;r<4;++r)for(int c=0;c<4;++c){
        float tolerance=.0001f+std::abs(native.projection[r][c])*.001f;
        if(!std::isfinite(matrix[r][c])||std::abs(matrix[r][c]-native.projection[r][c])>tolerance)return false;
    }
    return true;
}
// The native queue can execute after pG has advanced to another camera. Pair
// the exact queue record address and bytes with its enqueue-time view. Callers
// synchronize this bounded mailbox; the consumer takes each record once.
class QueuedWorldCameras {
    static constexpr size_t Ways=4,Buckets=4096;
    struct Entry{uintptr_t command{};uint64_t queued{},serial{},generation{};NativeSnapshot camera{};float matrix[4][4]{};};
    std::array<Entry,Ways*Buckets> entries_{};uint64_t serial_{};
    static size_t bucket(uintptr_t command){return (uint32_t((command>>2)*2654435761u)>>20)*Ways;}
public:
    void store(uintptr_t command,uint64_t queued,const float (&matrix)[4][4],const NativeSnapshot& camera,uint64_t generation=0){
        size_t start=bucket(command),choice=start;
        for(size_t i=start;i<start+Ways;++i){if(entries_[i].command==command||!entries_[i].command){choice=i;break;}if(entries_[i].serial<entries_[choice].serial)choice=i;}
        auto& entry=entries_[choice];entry.command=command;entry.queued=queued;entry.serial=++serial_;entry.generation=generation;entry.camera=camera;std::memcpy(entry.matrix,matrix,sizeof(matrix));
    }
    bool take(uintptr_t command,const float (&matrix)[4][4],NativeSnapshot& camera,uint64_t& queued,uint64_t generation=0){
        // At most four checks, independently of room object count. Each exact
        // reused address replaces its old record; colliding old records are
        // safely evicted instead of expanding memory or scanning the queue.
        size_t start=bucket(command);for(size_t n=start;n<start+Ways;++n){auto& entry=entries_[n];
            if(entry.command==command){bool equal=entry.generation==generation&&std::memcmp(entry.matrix,matrix,sizeof(matrix))==0;entry.command=0;
                if(equal){camera=entry.camera;queued=entry.queued;}return equal;}}
        return false;
    }
};
}
