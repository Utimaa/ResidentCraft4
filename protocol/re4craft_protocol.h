#pragma once
#include <cstdint>

// RE4Craft protocol v6: TCP 127.0.0.1:18044, little endian, bounded packets.
// Minecraft space: metres, +Y up. Avatar vertices are relative to player feet.
namespace re4craft {
constexpr uint32_t Magic = 0x4234434d; // bytes MC4B
constexpr uint16_t ProtocolVersion = 6;
constexpr uint16_t Port = 18044;
constexpr uint32_t MaxPayload = 32 * 1024 * 1024;
constexpr uint32_t MaxVertices = 300000;
constexpr uint32_t MaxBatches = 256;
// VehicleSurface13 remains indexed through the native renderer. These limits
// do not enlarge Minecraft/legacy mesh packets or the per-packet envelope.
constexpr uint32_t MaxVehicleVertices = 300000; // sum of batch palettes
constexpr uint32_t MaxVehicleIndices = 1500000; // complete source triangles
constexpr uint32_t MaxVehicleBatches = 1024;
constexpr uint32_t MaxVehicleSurfaceBytes = MaxPayload;
enum class Message : uint16_t { State=1, Texture=2, Mesh=3, Hud=4, Motion=5, Melee=6, Blocks=7, TextureChunk=8, VehicleInfo=9, VehicleMeshChunk=10, VehiclePose=11, VehicleMaterial=12, VehicleSurfaceChunk=13, VehicleTextureChunk=14, VehicleDashboard=15, Input=16, Goodbye=17, KeyEvent=18, Text=19, Collision=20, Enemies=21, Damage=22, Loot=23, MovingSurfaces=24 };
enum StateFlag : uint32_t { InWorld=1, ScreenOpen=2, OnGround=4, Crouching=8, Sprinting=16, Swimming=32, Dead=64 };
// NativeTraversal is Minecraft-only, combined with HostAuthority and never
// GuestAuthority. Its matching receipt grants no movement/combat authority.
enum InputFlag : uint32_t { Enabled=1, Focused=2, HostAuthority=4, Paused=8, GuestAuthority=16, NativeTraversal=32 };
enum HeldKey : uint32_t { Forward=1, Back=2, Left=4, Right=8, Jump=16, Sneak=32, Sprint=64, Attack=128, Use=256 };
enum PressedKey : uint32_t { Inventory=1, Drop=2, SwapHands=4, Camera=8, Escape=16, Chat=32, Command=64 };
enum MotionFlag : uint32_t { MotionReady=1, MotionGrounded=2, MotionDead=4, MotionScreen=8, MotionCameraReady=16, MotionNativeTraversalAck=32 };
// HitVehicle is exclusive and is accepted only from the dedicated BeamNG peer.
enum GuestHitFlag : uint32_t { HitCritical=1, HitProjectile=2, HitFire=4, HitHazard=8, HitExplosion=16, HitAlly=32, HitVehicle=64 };
enum HostDamageFlag : uint32_t { DamageFatal=1, DamageHasPosition=4 };
enum ActorFlag : uint32_t { ActorAlive=1, ActorDead=2, ActorBreakable=4, ActorHostile=8 };
enum BatchFlag : uint32_t { BatchDoubleSided=1, BatchTranslucent=2, BatchCrumbling=4, BatchParticle=8, BatchWhiteOverlay=16 };
#pragma pack(push, 1)
struct Header { uint32_t magic; uint16_t version; Message type; uint32_t size; uint32_t sequence; };
struct State {
    double x,y,z;
    float yaw,pitch,health,maxHealth,food,saturation,limbSwing,limbAmount;
    uint32_t flags,selectedSlot;
    uint64_t tick;
    uint32_t cameraMode,reserved; // 0 first person, 1 rear third person, 2 front third person
};
// Texture prefix followed by exactly width*height*4 RGBA8 bytes. ID0 = untextured.
struct Texture { uint32_t id,width,height; };
// BeamNG-only v6 extension: sequential lossless RGBA chunks, at most 1MiB each.
// Nothing is published until exactly width*height*4 bytes have arrived.
struct TextureChunk { uint32_t id,width,height,offset,byteCount,reserved; };
// BeamNG-only: flags1 acknowledges an actually constructed complete collider.
struct VehicleInfo { uint32_t epoch,vehicleId,nodeCount,flags; float collisionRadius; uint32_t reserved; };
// BeamNG-only: sequential <=64KiB fragments of a lossless indexed car mesh.
// Body: matching category1 Mesh, then IndexedBatch + Vertex palette + uint32
// triangle indices per batch. Only a fully validated frame becomes visible.
// An offset0 strictly newer epoch/frame can supersede an incomplete frame.
struct VehicleMeshChunk { uint32_t epoch,frame,totalBytes,offset,byteCount,reserved; };
// VehicleSurfaceChunk uses VehicleMeshChunk, followed by a complete body of
// Mesh + VehicleMeshCapture + IndexedVehicleBatch/vertices/indices. Capture
// and current poses use host Y-up, unit XYZW quaternions. Legacy mesh is unchanged.
struct VehicleMeshCapture { double sourceSeconds; float quaternion[4]; };
struct IndexedVehicleBatch { uint32_t textureId,flags,vertexCount,indexCount,materialId; };
// Visual samples only: Motion remains the physics/health/terrain authority.
struct VehiclePose {
    uint32_t epoch,vehicleId; uint64_t tick; double sourceSeconds;
    double hostX,hostY,hostZ; float quaternion[4];
    double cameraX,cameraY,cameraZ; float cameraQuaternion[4];
    float fov; uint32_t cameraMode; // 0 driver, 1 orbit, 2 chase
};
// Compact, lossless relevant data-map channels: 1 R8, 2 RG8, 4 RGBA8.
// Each chunk is <=64KiB; nothing is published until the complete image arrives.
// Original compact channels: R8=1, RG8=2, RGBA8=4; DDS-declared sRGB RGBA8=260.
// Bytes per pixel are format&255; bit256 controls hardware sRGB sampling.
struct VehicleTextureChunk { uint32_t id,width,height,offset,byteCount,format,reserved; };
// Active original material layers and actual linear vehicle paint channels.
// Header flags: double-sided=1, translucent=2, alpha-test=4,
// translucent depth-write=8, BlackSkyCubemap reflection=16; blend enum in bits8..10.
struct VehicleMaterial {
    uint32_t epoch,id,layerCount,flags; float alphaRef; uint32_t reserved;
    float paintColor[3][4],paintData[3][4];
};
struct VehicleMaterialLayer {
    uint32_t maps[11],flags;
    float baseColor[4],emissive[3],emissiveNits;
    float opacity,metallic,roughness,ao,clearcoat,clearcoatRoughness;
    float normalStrength,detailNormalStrength,detailScaleX,detailScaleY;
    uint32_t mapUV1Mask,reserved;
};
struct VehicleVertex { float x,y,z,nx,ny,nz,u,v,u2,v2; uint32_t color; };
// Availability bits8..15: RPM,maxRPM,gear,fuel,temp,damage,electrics,speed.
// Damage is actual dissipated energy (joules), not a percentage.
struct VehicleDashboard {
    uint32_t epoch,vehicleId; double sourceSeconds;
    float speed,rpm,maxRPM; int32_t gearIndex;
    float fuel,fuelCapacity; uint32_t flags;
    float engineTemperature,damage; uint32_t reserved[3];
};
struct Hud { uint32_t width,height,flags,frame; }; // bit0: bottom-up RGBA rows; bit1: RGBA32 RLE
struct KeyEvent { uint32_t scancode; int32_t action; uint32_t modifiers,reserved; };
struct Collision {
    uint32_t epoch,triangleCount,flags,reserved; // complete=1, truncated=2; Minecraft source IDs=4, reserved=revision
    float centerX,centerY,centerZ,radius; // host metres; complete coverage sphere
}; // followed by triangleCount*9 floats
// With Collision flag4, append triangleCount records {uint64 sourceKey,uint32 polygonIndex}.
// Only Minecraft uses the extension; other peers retain the original layout.
struct MovingSurfaces {uint32_t epoch,revision,count,reserved;uint64_t tick;};
struct MovingSurface {uint64_t sourceKey;uint32_t flags;float matrix[3][4];}; // removed registration=1; base-world -> current-world affine metres
// Mesh prefix followed by batch prefixes and each batch's Vertex array.
// Category1 replaces avatar; category2 replaces Minecraft scene (optional).
struct Mesh { uint32_t category,frame,batchCount,epoch; double hostOriginX,hostOriginY,hostOriginZ; };
struct Batch { uint32_t textureId,flags,vertexCount; };
struct IndexedBatch { uint32_t textureId,flags,vertexCount,indexCount; };
struct Vertex { float x,y,z,nx,ny,nz,u,v; uint32_t color; }; // color ARGB
struct GuestMotion {
    uint32_t epoch,flags; uint64_t tick;
    double hostX,hostY,hostZ,cameraX,cameraY,cameraZ;
    float velocityX,velocityY,velocityZ,yaw,pitch,health,maxHealth,cameraRoll,fov;
    uint32_t cameraMode,reserved;
};
struct GuestMelee {
    uint32_t epoch,targetId,eventId,flags;
    float damage,knockback;
    double attackX,attackY,attackZ;
    float directionX,directionY,directionZ,maxReach;
};
struct BlockColliders { uint32_t epoch,count; uint64_t tick; };
struct BlockBox { float minX,minY,minZ,maxX,maxY,maxZ; };
struct EnemySnapshot { uint32_t epoch,count; uint64_t tick; };
struct Enemy {
    uint32_t id,flags; double x,y,z;
    float minX,minY,minZ,maxX,maxY,maxZ,health,maxHealth,yaw;
    uint32_t kind;
};
struct HostDamage { uint32_t epoch,eventId; float amount; uint32_t kind,attackerId,flags; double sourceX,sourceY,sourceZ; };
// One verified native random drop, converted to real Minecraft item entities.
// source: Ganado1, crate2. tier: wood0, stone1, iron2, diamond3, netherite4, enchanted netherite5.
struct HostLoot { uint32_t epoch,eventId,source,tier; uint64_t sessionId; double x,y,z; };
struct Input {
    uint32_t flags,held,pressed;
    float yaw,pitch;
    uint32_t selectedSlot; // UINT32_MAX leaves selection unchanged
    int32_t scroll;
    uint32_t hostEpoch;
    double hostX,hostY,hostZ;
    uint32_t viewportWidth,viewportHeight;
    float cursorX,cursorY;
    uint32_t mouseButtons,reserved;
};
#pragma pack(pop)
static_assert(sizeof(Header)==16);
static_assert(sizeof(TextureChunk)==24);
static_assert(sizeof(VehicleInfo)==24);
static_assert(sizeof(VehicleMeshChunk)==24);
static_assert(sizeof(VehicleMeshCapture)==24);
static_assert(sizeof(IndexedVehicleBatch)==20);
static_assert(sizeof(VehiclePose)==112);
static_assert(sizeof(VehicleTextureChunk)==28);
static_assert(sizeof(VehicleMaterial)==120);
static_assert(sizeof(VehicleMaterialLayer)==128);
static_assert(sizeof(VehicleVertex)==44);
static_assert(sizeof(VehicleDashboard)==64);
static_assert(sizeof(IndexedBatch)==16);
static_assert(sizeof(State)==80);
static_assert(sizeof(Input)==80);
static_assert(sizeof(Mesh)==40);
static_assert(sizeof(Collision)==32);
static_assert(sizeof(MovingSurfaces)==24);
static_assert(sizeof(MovingSurface)==60);
static_assert(sizeof(GuestMotion)==108);
static_assert(sizeof(GuestMelee)==64);
static_assert(sizeof(Enemy)==72);
static_assert(sizeof(HostDamage)==48);
static_assert(sizeof(HostLoot)==48);
static_assert(sizeof(Vertex)==36);
}
