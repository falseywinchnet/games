#pragma once
// The ambient scene archive: a scene's prepared geometry, material maps, creatures
// and camera, written offline by a game's authoring script.
//
// File: a 32-byte header ("AMBSCN\0\1", version, payload bytes, compressed bytes,
// CRC-32 of the payload, 8 reserved) and one zlib stream. The payload is a
// sequence of tagged sections in a fixed order, little-endian IEEE754 throughout:
//
//   CAMR  eye, target, vertical field of view (degrees), near, far, light direction
//   TEXT  material maps: width, height, kind (0 albedo, 1 OpenGL normal map), RGB8 rows
//   STAT  fixed meshes in their own space, then placements (mesh, transform, tint, uv scale, material)
//   SWAY  foliage roots, then one world-space mesh whose vertices carry current-response data
//   AMSH  creature meshes in their own space
//   APRT  creature parts: mesh, transform within the creature, tint, material, creature, joint
//   ACTR  creatures: route centre and scale, cruise radii, speed and phase, kind
//   RISE  rising particles (bubbles): base, size, phase, speed, height
//
// Materials are numbers the game's Look interprets; the engine stores them.
#include "ambient_math.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ambient {

#pragma pack(push, 1)
struct StaticVertex {
    float position[3];
    std::int8_t normal[3];
    std::uint8_t moss;  // 0..255 surface coverage parameter
    float uv[2];
    std::uint8_t albedo[3];  // square-root encoded linear albedo
    std::uint8_t reserved;
};
struct StaticPlacementRecord {
    std::uint32_t mesh;
    float transform[12];
    float tint[3];
    float uv_scale[2];
    std::uint32_t material;
};
struct SwayVertex {
    float position[3];  // rest position, world space
    std::int8_t normal[3];
    std::uint8_t translucency;
    std::uint16_t uv[2];  // 0..65535 over the leaf
    std::uint8_t albedo[3];
    std::uint8_t compliance;  // 0..255 for 0..1.2
    std::int8_t bend[3];      // direction the current displaces this ribbon
    std::uint8_t reserved0;
    std::int8_t along[3];  // ribbon axis, for the bent normal
    std::uint8_t reserved1;
    float distance;  // distance from the root along the plant
    std::uint32_t root;
};
struct ActorVertex {
    float position[3];
    std::int8_t normal[3];
    std::uint8_t part;
    std::uint16_t uv[2];
    std::uint8_t fin;
    std::uint8_t reserved[3];
};
struct ActorPartRecord {
    float transform[12];
    float tint[3];
    std::uint32_t mesh;
    std::uint32_t material;
    std::int32_t actor;
    float joint;  // 0 rigid; other values are the Look's joint kinds
    float joint_phase;
};
struct ActorRecord {
    float center[4];  // xyz and creature scale
    float cruise[4];  // horizontal radius, vertical radius, angular speed, phase
    std::uint32_t kind;  // 0 swimmer, 1 walker (keeps its heading)
    std::uint32_t reserved[3];
};
struct RiserRecord {
    float base[3];
    float size;
    float phase;
    float speed;
    float height;
    float reserved;
};
#pragma pack(pop)
static_assert(sizeof(StaticVertex) == 28);
static_assert(sizeof(StaticPlacementRecord) == 76);
static_assert(sizeof(SwayVertex) == 40);
static_assert(sizeof(ActorVertex) == 24);
static_assert(sizeof(ActorPartRecord) == 80);
static_assert(sizeof(ActorRecord) == 48);
static_assert(sizeof(RiserRecord) == 32);

struct Texture {
    int width{};
    int height{};
    bool normal_map{};
    std::vector<std::uint8_t> rgb{};  // width * height * 3, row-major
};

template <typename Vertex> struct Mesh {
    std::vector<Vertex> vertices{};
    std::vector<std::uint32_t> indices{};  // triangle list
};

struct CameraSetup {
    Vec3 eye{};
    Vec3 target{};
    float field_of_view_degrees{45};
    float near_plane{0.1F};
    float far_plane{100};
    Vec3 light{0, 1, 0};  // direction toward the light
};

struct SceneData {
    CameraSetup camera{};
    std::vector<Texture> textures{};
    std::vector<Mesh<StaticVertex>> static_meshes{};
    std::vector<StaticPlacementRecord> placements{};
    std::vector<Vec3> sway_roots{};
    Mesh<SwayVertex> sway{};
    std::vector<Mesh<ActorVertex>> actor_meshes{};
    std::vector<ActorPartRecord> parts{};
    std::vector<ActorRecord> actors{};
    std::vector<RiserRecord> risers{};
};

// Parses a complete archive. Every count, index and float is checked before
// `destination` is replaced; on failure it is left unchanged and `error` says why.
[[nodiscard]] bool parse_scene(std::span<const std::uint8_t> file, SceneData& destination, std::string& error);
[[nodiscard]] bool load_scene(const std::string& path, SceneData& destination, std::string& error);

} // namespace ambient
