#include "archive.hpp"

#include "inflate.hpp"

#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace ambient {
namespace {

static_assert(std::endian::native == std::endian::little, "archives are little-endian");

// Generous bounds that keep a damaged file from requesting absurd allocations.
constexpr std::size_t max_payload_bytes = 256U * 1024U * 1024U;
constexpr std::uint32_t max_vertices = 4000000U;
constexpr std::uint32_t max_indices = 24000000U;
constexpr std::uint32_t max_records = 200000U;
constexpr int max_texture_side = 4096;

class Cursor final {
  public:
    explicit Cursor(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
    [[nodiscard]] bool read(void* destination, std::size_t count) {
        if (count > bytes_.size() - position_)
            return false;
        std::memcpy(destination, bytes_.data() + position_, count);
        position_ += count;
        return true;
    }
    [[nodiscard]] bool u32(std::uint32_t& value) {
        const bool ok = read(&value, sizeof(value));
        return ok;
    }
    [[nodiscard]] bool tag(const char* expected, std::uint32_t& count) {
        char found[4]{};
        if (!read(found, 4) || std::memcmp(found, expected, 4) != 0)
            return false;
        const bool ok = u32(count);
        return ok;
    }
    template <typename Record> [[nodiscard]] bool records(std::vector<Record>& out, std::uint32_t count) {
        // Callers bound their counts; this keeps every read inside the payload.
        if (static_cast<std::size_t>(count) * sizeof(Record) > bytes_.size() - position_)
            return false;
        out.resize(count);
        const bool ok = read(out.data(), static_cast<std::size_t>(count) * sizeof(Record));
        return ok;
    }
    [[nodiscard]] bool finished() const {
        return position_ == bytes_.size();
    }

  private:
    std::span<const std::uint8_t> bytes_;
    std::size_t position_{};
};

bool finite(const float* values, std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
        if (!std::isfinite(values[index]))
            return false;
    }
    return true;
}

template <typename Vertex> bool read_mesh(Cursor& cursor, Mesh<Vertex>& mesh) {
    std::uint32_t vertex_count = 0;
    std::uint32_t index_count = 0;
    if (!cursor.u32(vertex_count) || !cursor.u32(index_count))
        return false;
    if (vertex_count == 0 || vertex_count > max_vertices || index_count == 0 || index_count > max_indices ||
        index_count % 3 != 0)
        return false;
    if (!cursor.records(mesh.vertices, vertex_count))
        return false;
    if (static_cast<std::size_t>(index_count) > max_indices || !cursor.records(mesh.indices, index_count))
        return false;
    for (const std::uint32_t index : mesh.indices) {
        if (index >= vertex_count)
            return false;
    }
    for (const Vertex& vertex : mesh.vertices) {
        if (!finite(vertex.position, 3))
            return false;
    }
    return true;
}

bool read_camera(Cursor& cursor, CameraSetup& camera) {
    std::uint32_t count = 0;
    float values[12]{};
    if (!cursor.tag("CAMR", count) || count != 1 || !cursor.read(values, sizeof(values)) || !finite(values, 12))
        return false;
    camera.eye = {values[0], values[1], values[2]};
    camera.target = {values[3], values[4], values[5]};
    camera.field_of_view_degrees = values[6];
    camera.near_plane = values[7];
    camera.far_plane = values[8];
    camera.light = normalize({values[9], values[10], values[11]});
    const bool sensible = camera.field_of_view_degrees > 1 && camera.field_of_view_degrees < 170 &&
                          camera.near_plane > 0 && camera.far_plane > camera.near_plane &&
                          length(subtract(camera.target, camera.eye)) > 1e-4F && length(camera.light) > 0.5F;
    return sensible;
}

bool read_textures(Cursor& cursor, std::vector<Texture>& textures) {
    std::uint32_t count = 0;
    if (!cursor.tag("TEXT", count) || count > 64)
        return false;
    textures.resize(count);
    for (Texture& texture : textures) {
        std::uint16_t size[2]{};
        std::uint32_t kind = 0;
        if (!cursor.read(size, sizeof(size)) || !cursor.u32(kind) || kind > 1)
            return false;
        texture.width = size[0];
        texture.height = size[1];
        texture.normal_map = kind == 1;
        // Power-of-two sides let samplers wrap with a mask.
        if (texture.width < 1 || texture.width > max_texture_side || texture.height < 1 ||
            texture.height > max_texture_side || !std::has_single_bit(static_cast<unsigned>(texture.width)) ||
            !std::has_single_bit(static_cast<unsigned>(texture.height)))
            return false;
        const std::uint32_t bytes = static_cast<std::uint32_t>(texture.width * texture.height * 3);
        if (!cursor.records(texture.rgb, bytes))
            return false;
    }
    return true;
}

bool read_static(Cursor& cursor, SceneData& scene) {
    std::uint32_t count = 0;
    if (!cursor.tag("STAT", count) || count == 0 || count > 4096)
        return false;
    scene.static_meshes.resize(count);
    for (Mesh<StaticVertex>& mesh : scene.static_meshes) {
        if (!read_mesh(cursor, mesh))
            return false;
        for (const StaticVertex& vertex : mesh.vertices) {
            if (!finite(vertex.uv, 2))
                return false;
        }
    }
    std::uint32_t placements = 0;
    if (!cursor.u32(placements) || placements > max_records || !cursor.records(scene.placements, placements))
        return false;
    for (const StaticPlacementRecord& placement : scene.placements) {
        if (placement.mesh >= count || !finite(placement.transform, 12) || !finite(placement.tint, 3) ||
            !finite(placement.uv_scale, 2))
            return false;
    }
    return true;
}

bool read_sway(Cursor& cursor, SceneData& scene) {
    std::uint32_t count = 0;
    std::uint32_t roots = 0;
    if (!cursor.tag("SWAY", count) || count != 1 || !cursor.u32(roots) || roots == 0 || roots > max_records)
        return false;
    std::vector<float> root_values{};
    if (!cursor.records(root_values, roots * 3U) || !finite(root_values.data(), root_values.size()))
        return false;
    scene.sway_roots.resize(roots);
    for (std::size_t index = 0; index < roots; ++index)
        scene.sway_roots[index] = {root_values[index * 3], root_values[index * 3 + 1], root_values[index * 3 + 2]};
    if (!read_mesh(cursor, scene.sway))
        return false;
    for (const SwayVertex& vertex : scene.sway.vertices) {
        if (vertex.root >= roots || !std::isfinite(vertex.distance) || vertex.distance < 0)
            return false;
    }
    return true;
}

bool read_actors(Cursor& cursor, SceneData& scene) {
    std::uint32_t count = 0;
    if (!cursor.tag("AMSH", count) || count == 0 || count > 256)
        return false;
    scene.actor_meshes.resize(count);
    for (Mesh<ActorVertex>& mesh : scene.actor_meshes) {
        if (!read_mesh(cursor, mesh))
            return false;
    }
    std::uint32_t parts = 0;
    std::uint32_t actors = 0;
    if (!cursor.tag("APRT", parts) || parts > max_records || !cursor.records(scene.parts, parts))
        return false;
    if (!cursor.tag("ACTR", actors) || actors > 4096 || !cursor.records(scene.actors, actors))
        return false;
    for (const ActorPartRecord& part : scene.parts) {
        if (part.mesh >= count || part.actor < 0 || static_cast<std::uint32_t>(part.actor) >= actors ||
            !finite(part.transform, 12) || !finite(part.tint, 3) || !std::isfinite(part.joint) ||
            !std::isfinite(part.joint_phase))
            return false;
    }
    for (const ActorRecord& actor : scene.actors) {
        if (!finite(actor.center, 4) || !finite(actor.cruise, 4) || actor.kind > 1 || actor.center[3] <= 0)
            return false;
    }
    return true;
}

bool read_risers(Cursor& cursor, SceneData& scene) {
    std::uint32_t count = 0;
    if (!cursor.tag("RISE", count) || count > 4096 || !cursor.records(scene.risers, count))
        return false;
    for (const RiserRecord& riser : scene.risers) {
        if (!finite(riser.base, 3) || !std::isfinite(riser.size) || !std::isfinite(riser.phase) ||
            !std::isfinite(riser.speed) || !std::isfinite(riser.height) || riser.size <= 0 || riser.height <= 0)
            return false;
    }
    return true;
}

} // namespace

bool parse_scene(std::span<const std::uint8_t> file, SceneData& destination, std::string& error) {
    error = "The scene archive is damaged or from an unknown version";
    if (file.size() < 32 || std::memcmp(file.data(), "AMBSCN\0\1", 8) != 0)
        return false;
    std::uint32_t header[4]{};
    std::memcpy(header, file.data() + 8, sizeof(header));
    const std::uint32_t version = header[0];
    const std::uint32_t payload_bytes = header[1];
    const std::uint32_t compressed_bytes = header[2];
    const std::uint32_t checksum = header[3];
    if (version != 1 || payload_bytes == 0 || payload_bytes > max_payload_bytes ||
        compressed_bytes != file.size() - 32)
        return false;
    std::vector<std::uint8_t> payload{};
    if (!inflate_zlib(file.subspan(32), payload_bytes, payload) || crc32(payload) != checksum)
        return false;
    SceneData scene{};
    Cursor cursor(payload);
    const bool ok = read_camera(cursor, scene.camera) && read_textures(cursor, scene.textures) &&
                    read_static(cursor, scene) && read_sway(cursor, scene) && read_actors(cursor, scene) &&
                    read_risers(cursor, scene) && cursor.finished();
    if (!ok)
        return false;
    destination = std::move(scene);
    error.clear();
    return true;
}

bool load_scene(const std::string& path, SceneData& destination, std::string& error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "The scene archive could not be opened";
        return false;
    }
    std::vector<std::uint8_t> file((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    const bool ok = parse_scene(file, destination, error);
    return ok;
}

} // namespace ambient
