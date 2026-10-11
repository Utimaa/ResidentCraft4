#pragma once
#include "game_access.hpp"
#include <cmath>
#include <cstring>

namespace re4craft {
// Use the projection already calculated by actual GXSetProjection. This
// preserves the native near/far arithmetic and its final float rounding.
inline bool composeExecutedRasterProjection(NativeSnapshot& native,const float (&rasterColumns)[4][4],const float (&projectionColumns)[4][4]) {
    native.rasterProjectionValid=false;
    for(const auto& column:rasterColumns)for(float value:column)if(!std::isfinite(value)||std::abs(value)>10000.f)return false;
    for(const auto& column:projectionColumns)for(float value:column)if(!std::isfinite(value)||std::abs(value)>1e8f)return false;
    float result[4][4]{};
    for(int row=0;row<4;++row)for(int column=0;column<4;++column){double value{};
        for(int k=0;k<4;++k)value+=double(rasterColumns[k][row])*projectionColumns[column][k];
        if(!std::isfinite(value)||std::abs(value)>1e8)return false;result[row][column]=static_cast<float>(value);}
    std::memcpy(native.rasterProjection,result,sizeof(result));native.rasterProjectionValid=true;return true;
}
// GX's matrix builder stores the four raster matrix columns consecutively.
// Compose the same R * converted(GX projection) without inferring a zoom from
// viewport dimensions. The result already uses Direct3D [0,1] clip depth.
inline bool composeRasterProjection(NativeSnapshot& native,const float (&columns)[4][4]) {
    native.rasterProjectionValid=false;
    for(const auto& column:columns)for(float value:column)if(!std::isfinite(value)||std::abs(value)>10000.f)return false;
    float result[4][4]{};
    for(int row=0;row<4;++row)for(int column=0;column<4;++column){
        double value{};
        for(int k=0;k<4;++k)value+=double(columns[k][row])*(native.projection[k][column]+(k==2?native.projection[3][column]:0.f));
        if(!std::isfinite(value)||std::abs(value)>1e8)return false;
        result[row][column]=static_cast<float>(value);
    }
    for(int row=0;row<4;++row)for(int column=0;column<4;++column)native.rasterProjection[row][column]=result[row][column];
    native.rasterProjectionValid=true;return true;
}
}
