#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string_view>

namespace re4craft {
// BeamNG's default SDR post-process uses the ACES fitted display transform.
// Source: installed shaders/common/postFx/hdr/{ACES,tonemap}.h.hlsl.
// The fit is Stephen Hill's MIT-licensed implementation from Baking Lab by
// MJP and David Neubelt. See THIRD-PARTY-NOTICES.md. Exposure/lighting remain
// the bridge's approximation; source paint and texture values are untouched.
// BeamNG applies scene exposure before ACES. Its actual scene exposure is not
// part of the passthrough, so this is an explicit neutral bridge calibration:
// -1 EV reduces the fixed sky/sun approximation without altering source color.
inline constexpr float VehicleDisplayExposureScale=.5f;
inline constexpr float VehicleDisplayExposureMinimum=1.f/16.f;
inline constexpr float VehicleDisplayExposureMaximum=16.f;
inline bool validVehicleDisplayExposure(float scale){
    return std::isfinite(scale)&&scale>=VehicleDisplayExposureMinimum&&scale<=VehicleDisplayExposureMaximum;
}
inline std::array<float,3> vehicleToneMapDisplayLinear(std::array<float,3> color){
    for(auto& value:color)value=std::max(value,0.f);
    const auto input=color;
    color={.59719f*input[0]+.35458f*input[1]+.04823f*input[2],
           .07600f*input[0]+.90834f*input[1]+.01566f*input[2],
           .02840f*input[0]+.13383f*input[1]+.83777f*input[2]};
    for(auto& value:color){
        const float numerator=value*(value+.0245786f)-.000090537f;
        const float denominator=value*(.983729f*value+.4329510f)+.238081f;
        value=numerator/denominator;
    }
    const auto fitted=color;
    color={1.60475f*fitted[0]-.53108f*fitted[1]-.07367f*fitted[2],
           -.10208f*fitted[0]+1.10813f*fitted[1]-.00605f*fitted[2],
           -.00327f*fitted[0]-.07276f*fitted[1]+1.07602f*fitted[2]};
    for(auto& value:color)value=std::clamp(value,0.f,1.f);
    return color;
}
inline std::array<float,3> vehicleDisplayLinear(std::array<float,3> hdr,float exposureScale=VehicleDisplayExposureScale){
    if(!validVehicleDisplayExposure(exposureScale))throw std::invalid_argument("Vehicle display exposure outside finite 1/16..16 bounds");
    for(auto& value:hdr){
        if(!std::isfinite(value))throw std::invalid_argument("Nonfinite vehicle display radiance");
        value*=exposureScale;
    }
    return vehicleToneMapDisplayLinear(hdr);
}
inline std::array<float,3> vehicleDisplaySrgb(std::array<float,3> hdr,float exposureScale=VehicleDisplayExposureScale){
    auto color=vehicleDisplayLinear(hdr,exposureScale);
    for(auto& value:color)value=value<=.0031308f?value*12.92f:1.055f*std::pow(value,1.f/2.4f)-.055f;
    return color;
}
inline constexpr std::string_view VehicleDisplayToneMapHlsl=R"HLSL(
float3 vehicleToneMapDisplayLinear(float3 color){
    color=max(color,0);
    color=mul(float3x3(.59719,.35458,.04823,.07600,.90834,.01566,.02840,.13383,.83777),color);
    color=(color*(color+.0245786)-.000090537)/(color*(.983729*color+.4329510)+.238081);
    return saturate(mul(float3x3(1.60475,-.53108,-.07367,-.10208,1.10813,-.00605,-.00327,-.07276,1.07602),color));
}
// Fixed -1 EV bridge calibration; this is not captured source/host exposure.
float3 vehicleDisplayLinear(float3 hdr){return vehicleToneMapDisplayLinear(hdr*.5);}
)HLSL";
}
