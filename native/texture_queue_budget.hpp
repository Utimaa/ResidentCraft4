#pragma once
#include <cstddef>

namespace re4craft {
// A completed undrained same-ID candidate is replaced by the new partial.
// Already uploaded renderer revisions live in an independent cache.
inline bool vehicleTextureRevisionFits(size_t completeBytes,size_t partialBytes,
    size_t completeImages,size_t partialImages,size_t retiredBytes,bool retiring,size_t replacementBytes){
    constexpr size_t maxBytes=384u*1024u*1024u,maxImages=256,maxPartials=8;
    if(partialImages>=maxPartials||completeImages>maxImages||partialImages>maxImages||
       completeBytes>maxBytes||partialBytes>maxBytes||retiredBytes>completeBytes||
       (retiring?!completeImages:retiredBytes!=0))return false;
    const size_t remainingImages=completeImages-(retiring?1:0);
    if(remainingImages>=maxImages||partialImages>=maxImages-remainingImages)return false;
    const size_t remainingBytes=completeBytes-retiredBytes;
    return partialBytes<=maxBytes-remainingBytes&&replacementBytes<=maxBytes-remainingBytes-partialBytes;
}
}
