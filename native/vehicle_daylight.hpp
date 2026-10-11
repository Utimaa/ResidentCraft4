#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace re4craft {
// The owned source level has a fixed midday Earth sky. This inexpensive native
// approximation supplies daylight fill/reflections; it is not a captured sky or
// source exposure. All values are linear radiance, not encoded sRGB texels.
inline constexpr float VehicleDaylightDirectScale=2.2f;
inline constexpr float VehicleDaylightAmbientScale=1.1f;
inline constexpr float VehicleDaylightDirectSpecularScale=.6f;
inline constexpr float VehicleDaylightReflectionScale=.55f;
inline constexpr std::array<float,3> VehicleDaylightGround{.26f,.255f,.245f};
inline constexpr std::array<float,3> VehicleDaylightHorizon{.62f,.63f,.64f};
inline constexpr std::array<float,3> VehicleDaylightZenith{.62f,.64f,.68f};
inline std::array<float,3> vehicleDaylightEnvironment(float vertical){
    vertical=std::clamp(vertical,-1.f,1.f);
    const auto& endpoint=vertical<0?VehicleDaylightGround:VehicleDaylightZenith;
    const float amount=std::abs(vertical);
    std::array<float,3> result{};
    for(unsigned channel=0;channel<3;channel++)
        result[channel]=VehicleDaylightHorizon[channel]+(endpoint[channel]-VehicleDaylightHorizon[channel])*amount;
    return result;
}
inline constexpr std::string_view VehicleDaylightHlsl=R"HLSL(
// Fixed soft daylight approximation. Neutral diffuse fill keeps dark surfaces
// readable; specular-only calibration reduces the broad white reflection veil.
// Source paint, metal/roughness/clearcoat maps and tone exposure are unchanged.
static const float vehicleDaylightDirectScale=2.2;
static const float vehicleDaylightAmbientScale=1.1;
static const float vehicleDaylightDirectSpecularScale=.6;
static const float vehicleDaylightReflectionScale=.55;
float3 vehicleDaylightSunDirection(){return normalize(float3(-.35,.8,-.45));}
)HLSL";
}
