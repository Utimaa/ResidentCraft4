#pragma once
#include "game_access.hpp"
#include "vehicle_presentation.hpp"
#include <cstdint>
#include <mutex>

namespace re4craft {
enum class CameraMode : uint32_t { FirstPerson=0, ThirdPersonBack=1, ThirdPersonFront=2 };
struct InputLook { float yawDegrees{}, pitchDegrees{}; };
struct InputMouseDelta { float x{},y{}; };
struct CameraSettings {
    bool active{};
    CameraMode mode{CameraMode::ThirdPersonBack};
    float yawDegrees{}, pitchDegrees{}, eyeHeightBlocks{1.62f};
    float worldUnitsPerBlock{1000.f}, thirdPersonDistanceBlocks{4.f}, fovDegrees{70.f};
    bool guestCameraValid{};
    Vec3 guestCameraPosition{}; // absolute RE4 world units, already clipped/bobbed by Minecraft
    float guestYawDegrees{},guestPitchDegrees{},guestRollDegrees{};
    bool vehiclePresentationValid{};
    VehiclePresentationSample vehiclePresentation{};
};
struct CameraPoint { Vec3 position{}, target{}; float roll{}, fovDegrees{}; };
static_assert(sizeof(CameraPoint)==0x20);
// Produces world coordinates consumed by RE4's native camera calculation.
bool calculateCameraPoint(const CameraSettings&, Vec3 feet, CameraPoint& output);
// The input poll uses this same source selection and native-delta conversion.
// A recent WM_INPUT event suppresses its duplicate DirectInput sample for 50ms.
InputLook advanceNativeLook(InputLook current,InputMouseDelta nativeDelta,float nativeDeltaTime,bool allowLook,bool rawAvailable,uint64_t lastRawTime,uint64_t now);
class CameraControlBridge {
public:
    bool installHooks(GameAccess* game, LogFn log=nullptr);
    void setState(const CameraSettings&);
    void addMouseDelta(float dx,float dy,float sensitivityDegreesPerCount=0.15f);
    InputLook look() const;
    InputMouseDelta readMouseDelta() const;
    void onInputPoll(bool allowLook=true);
    void restore();
    bool installed() const { return installed_; }
    // Called at the verified CameraSmooth::move boundary, before world rendering.
    void apply(void* smooth, CameraPoint* nativePoint);
    bool appliedVehiclePresentation(const NativeSnapshot&,uint32_t epoch,VehiclePresentationSample& output)const;
    // Bounded read-only explanation of a rejected exact camera pair.
    AppliedVehicleCameraDiagnostic appliedVehicleDiagnostic(const NativeSnapshot&,uint32_t epoch,bool& active)const;
private:
    mutable std::mutex mutex_;
    CameraSettings settings_{};
    GameAccess* game_{};
    bool installed_{}, wasApplied_{};
    bool rawSincePoll_{}, ownedYaw_{};
    bool rawAvailable_{};
    uintptr_t mouseDelta_{}, playerWithYaw_{};
    float originalYaw_{};
    float lastWrittenYaw_{};
    uint64_t lastStateTime_{},lastRawTime_{};
    AppliedVehiclePresentationHistory appliedVehiclePresentations_;
    void restoreYawLocked(const NativeSnapshot& native);
};
}
