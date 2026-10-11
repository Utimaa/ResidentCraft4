#pragma once
#include <d3d9.h>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <memory>
#include <string>
#include <array>
#include "game_access.hpp"
#include "vehicle_presentation.hpp"
#include "../protocol/re4craft_protocol.h"

namespace re4craft {
struct RenderBatch {
    uint32_t textureId{},flags{}; std::vector<Vertex> vertices;
    uint32_t materialId{}; std::vector<std::array<float,2>> secondaryUV;
    std::vector<uint32_t> sourceIndices; // actual palette index per expanded vertex
    // Surface13 palettes stay compact; all actual triangle indices are kept.
    // Empty retains the legacy expanded triangle-list representation.
    std::vector<uint32_t> indices;
};
struct RenderPlacement {
    Vec3 origin{};
    float yawRadians{};
    float worldUnitsPerBlock{1000.f};
    float ySign{1.f};
    bool drawScene{};
    bool drawAvatar{true};
    uint32_t hostEpoch{}; // zero allows unanchored standalone fixtures
    bool drawDynamic{true};
    VehicleQuaternion orientation{};
    bool useOrientation{};
};
struct RendererResourceUsage {
    size_t cpuTextureBytes{},cpuTextureCapacityBytes{},gpuTextureCount{},gpuMaterialCount{};
    size_t decodedTextureBytes{};
    bool pendingClear{};
};
class Renderer {
public:
    // Optional car atlas is used only for measured comparisons. The default
    // retains source UV precision and avoids an extra 64MiB texture allocation.
    explicit Renderer(bool vehicleAssets=false,bool vehicleAtlas=false);
    ~Renderer();
    // Thread safe packet ingestion; Direct3D calls are deferred to render().
    bool uploadTexture(uint32_t id,uint32_t width,uint32_t height,const uint8_t* pixels,size_t byteCount,uint32_t format=4);
    // Completed packet ownership transfers without a second full pixel buffer.
    // A rejected upload leaves pixels unchanged. No Direct3D calls occur here.
    bool uploadTexture(uint32_t id,uint32_t width,uint32_t height,std::vector<uint8_t>&& pixels,uint32_t format=4);
    // Completed vehicle maps may retain losslessly packed CPU storage. Decode
    // one original map only for GPU upload/Reset, never on each warm frame.
    bool uploadVehicleTexture(uint32_t id,uint32_t width,uint32_t height,std::vector<uint8_t>&& storage,uint32_t format,bool compressed);
    bool setMesh(uint32_t category,std::vector<RenderBatch> batches);
    bool setVehicleFrame(std::vector<RenderBatch> batches,VehicleQuaternion capturedOrientation,double captureSeconds,uint32_t epoch,uint32_t vehicleId=0,double arrivalMs=-1);
    void setVehiclePose(Vec3 nativeUnits,VehicleQuaternion presentationOrientation,uint32_t epoch,uint32_t vehicleId=0,double sourceSeconds=-1);
    // Repairs can retain vehicle ID/epoch. Retire only visual endpoints;
    // current source geometry, materials and texture maps stay available.
    void resetVehicleDeformation();
    bool setVehicleMaterial(const VehicleMaterial& header,const std::vector<VehicleMaterialLayer>& layers);
    void setAvatarOrigin(Vec3 nativeUnits,uint32_t epoch);
    void setSceneOrigin(Vec3 nativeUnits,uint32_t epoch);
    void setDynamicOrigin(Vec3 nativeUnits,uint32_t epoch);
    void clear();
    // Call on the render/reset thread even when no world pass is eligible.
    // Retires cleared session resources without discarding newer CPU packets.
    void retirePendingResources();
    RendererResourceUsage resourceUsage() const;
    bool hasAvatar() const;
    // Return value means submitted geometry. worldValid separately confirms
    // the depth/projection pass, including legitimately culled/empty scenes.
    bool render(IDirect3DDevice9* device,const NativeSnapshot& native,const RenderPlacement& placement,bool* worldValid=nullptr,double presentationMs=-1);
    // One-line activation diagnostics: real target/depth/viewport and clip bounds.
    std::string describeFrame(IDirect3DDevice9* device,const NativeSnapshot& native,const RenderPlacement& placement) const;
    void onDeviceLost(); // Call on render/reset thread before Reset.
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
