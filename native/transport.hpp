#pragma once
#include "../protocol/re4craft_protocol.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
#include <deque>
#include <array>

namespace re4craft {
struct MeshBatch {
    uint32_t textureId{},flags{};std::vector<Vertex> vertices;uint32_t materialId{};
    std::vector<std::array<float,2>> secondaryUV;
    std::vector<uint32_t> sourceIndices; // optional original IDs for legacy expanded geometry
    std::vector<uint32_t> indices; // Surface13: exact triangles into vertices/secondaryUV palettes
};
struct TextureData {
    uint32_t id{},width{},height{};std::vector<uint8_t> rgba;uint32_t format{4};
    bool compressed{}; // internal lossless storage only; wire pixels unchanged
};
struct VehicleMaterialData { VehicleMaterial header{}; std::vector<VehicleMaterialLayer> layers; };
struct VehiclePoseUpdate { VehiclePose pose{}; double arrivalMs{}; }; // steady_clock, presentation only
struct HudData {
    uint32_t width{},height{},flags{},frame{};std::vector<uint8_t> rgba;
    bool uiModeKnown{},uiScreenOpen{}; // owned BeamNG mapping metadata only
};
struct Updates {
    bool connected{},stateValid{},meshValid{},newMesh{},newScene{},newEntities{},newHud{},hudValid{},newSession{};
    State state{};
    std::vector<MeshBatch> mesh;
    std::vector<TextureData> textures;
    std::vector<MeshBatch> scene;
    std::vector<MeshBatch> entities;
    HudData hud;
    Mesh sceneHeader{};
    Mesh meshHeader{};
    Mesh entityHeader{};
    bool motionValid{},newMotion{},newBlocks{};
    uint32_t stateAgeMs{0xffffffff},meshAgeMs{0xffffffff},hudAgeMs{0xffffffff},motionAgeMs{0xffffffff};
    bool hudUiModeKnown{},hudUiScreenOpen{};
    GuestMotion motion{};
    VehicleInfo vehicle{};
    bool vehicleValid{};
    bool vehiclePoseValid{},surfaceMesh{};
    // Read-only transport diagnostics. These retain the accepted pose after
    // its one-shot queue is drained; they grant no presentation authority.
    uint32_t vehicleAgeMs{0xffffffff},vehiclePoseAgeMs{0xffffffff};
    VehiclePose latestVehiclePose{};
    uint64_t vehiclePoseAccepted{},vehiclePoseIgnored{};
    VehicleMeshCapture meshCapture{};
    std::vector<VehiclePoseUpdate> vehiclePoses;
    std::vector<VehicleMaterialData> vehicleMaterials;
    VehicleDashboard vehicleDashboard{};
    bool vehicleDashboardValid{},newVehicleDashboard{};
    uint32_t blockEpoch{};
    std::vector<BlockBox> blocks;
    std::vector<GuestMelee> melee;
};
class Transport {
public:
    explicit Transport(std::function<void(const char*)> log = {},bool vehiclePeer=false,bool compactVehicleTextures=false):log_(std::move(log)),vehiclePeer_(vehiclePeer),compactVehicleTextures_(vehiclePeer&&compactVehicleTextures){}
    ~Transport(){stop();}
    void start(uint16_t port=Port);
    void stop();
    void setInput(const Input& input);
    bool sendPacket(Message type,std::vector<uint8_t> payload);
    Updates takeUpdates();
    bool connected() const {return connected_.load();}
    // Shared with the external harness: validates actual production decoder.
    bool accept(Message type,const std::vector<uint8_t>& payload);
private:
    friend struct TransportAllocationTestAccess;
    bool acceptBytes(Message type,const uint8_t* source,size_t size);
    void run(uint16_t port);
    void reset(bool releaseInput=false);
    void note(const char* message){if(log_)log_(message);}
    std::function<void(const char*)> log_;
    const bool vehiclePeer_{};
    const bool compactVehicleTextures_{};
    uint64_t textureSession_{},textureSerial_{};
    std::map<uint32_t,uint64_t> latestTextureSerial_;
    // The protocol fixture alone can inject failure at the actual texture
    // allocation. Production instances leave this null; it changes no bounds.
    void (*textureAllocationCheck_)(size_t){};
    void (*textureCompressionCheck_)(){};
    unsigned allocationFailures_{};
    std::atomic<bool> stopping_{false},connected_{false};
    std::thread thread_;
    std::mutex mutex_;
    State state_{};
    Input input_{};
    std::vector<MeshBatch> mesh_;
    std::vector<MeshBatch> scene_;
    std::vector<MeshBatch> entities_;
    HudData hud_;
    Mesh sceneHeader_{};
    Mesh meshHeader_{};
    Mesh entityHeader_{};
    GuestMotion motion_{};
    VehicleInfo vehicle_{};
    VehiclePose vehiclePose_{};
    std::vector<VehiclePoseUpdate> vehiclePoses_;
    uint64_t vehiclePoseAccepted_{},vehiclePoseIgnored_{};
    VehicleMeshCapture meshCapture_{};
    bool surfaceMesh_{};
    std::map<uint32_t,VehicleMaterialData> vehicleMaterials_;
    VehicleDashboard vehicleDashboard_{};
    bool newVehicleDashboard_{};
    uint32_t blockEpoch_{};
    std::vector<BlockBox> blocks_;
    std::vector<GuestMelee> melee_;
    struct Outgoing { Message type; std::vector<uint8_t> payload; };
    std::deque<Outgoing> outgoing_;
    std::map<uint32_t,TextureData> textures_;
    struct PartialTexture { TextureData image; uint32_t received{}; };
    std::map<uint32_t,PartialTexture> partialTextures_;
    struct PartialVehicleMesh {
        uint32_t epoch{},frame{},totalBytes{},received{};
        Message kind{Message::VehicleMeshChunk};
        std::vector<uint8_t> bytes;
    } partialVehicleMesh_;
    bool newMesh_{},newScene_{},newEntities_{},newHud_{},newSession_{};
    bool newMotion_{},newBlocks_{};
    uint64_t uiGeneration_{};
    std::chrono::steady_clock::time_point stateTime_{},meshTime_{};
    std::chrono::steady_clock::time_point inputTime_{};
    std::chrono::steady_clock::time_point hudTime_{};
    std::chrono::steady_clock::time_point motionTime_{};
    std::chrono::steady_clock::time_point vehicleTime_{};
    std::chrono::steady_clock::time_point vehiclePoseTime_{};
    std::chrono::steady_clock::time_point vehicleDashboardTime_{};
};
}
