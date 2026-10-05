// The ambient engine's own acceptance tests: decoding, rasterization, archive
// validation, motion, cadence, presentation and an end-to-end synthetic stage.
// No toolkit and no files are needed.
#include "archive.hpp"
#include "cadence.hpp"
#include "inflate.hpp"
#include "motion.hpp"
#include "present.hpp"
#include "raster3d.hpp"
#include "settings.hpp"
#include "stage.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace ambient;

int checks = 0;
void require(bool condition, const char* what) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        std::abort();
    }
}

// ------------------------------------------------------------------ inflate

std::vector<std::uint8_t> dynamic_vector_input() {
    std::vector<std::uint8_t> data{};
    for (int i = 0; i < 3000; ++i)
        data.push_back(static_cast<std::uint8_t>(((i * 7) % 251) ^ ((i / 13) % 7)));
    const std::string phrase = "the quiet tank hums; ";
    for (int k = 0; k < 40; ++k)
        data.insert(data.end(), phrase.begin(), phrase.end());
    return data;
}

const std::vector<std::uint8_t> dynamic_vector{
#include "dynamic_vector.inc"
};

std::uint32_t adler(const std::vector<std::uint8_t>& bytes) {
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const std::uint8_t byte : bytes) {
        a = (a + byte) % 65521U;
        b = (b + a) % 65521U;
    }
    return (b << 16U) | a;
}

// A zlib stream of stored (uncompressed) blocks: what a test can write without an encoder.
std::vector<std::uint8_t> stored_zlib(const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> out{0x78, 0x01};
    std::size_t offset = 0;
    do {
        const std::size_t length = std::min<std::size_t>(65535, payload.size() - offset);
        const bool last = offset + length == payload.size();
        out.push_back(last ? 1 : 0);
        out.push_back(static_cast<std::uint8_t>(length & 255U));
        out.push_back(static_cast<std::uint8_t>(length >> 8U));
        out.push_back(static_cast<std::uint8_t>(~length & 255U));
        out.push_back(static_cast<std::uint8_t>((~length >> 8U) & 255U));
        out.insert(out.end(), payload.begin() + static_cast<std::ptrdiff_t>(offset),
                   payload.begin() + static_cast<std::ptrdiff_t>(offset + length));
        offset += length;
    } while (offset < payload.size());
    const std::uint32_t check = adler(payload);
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(static_cast<std::uint8_t>(check >> shift));
    return out;
}

void test_inflate() {
    const std::string text = "123456789";
    const std::vector<std::uint8_t> digits(text.begin(), text.end());
    require(crc32(digits) == 0xCBF43926U, "CRC-32 check value");

    // Fixed-Huffman block with back-references: zlib.compress(b"hello hello hello hello", 9).
    const std::vector<std::uint8_t> fixed{120, 218, 203, 72, 205, 201, 201, 87, 200, 64, 39, 1, 104, 3, 8, 177};
    std::vector<std::uint8_t> out{};
    require(inflate_zlib(fixed, 23, out), "fixed-Huffman stream decodes");
    require(std::string(out.begin(), out.end()) == "hello hello hello hello", "fixed-Huffman content");

    const std::vector<std::uint8_t> expected = dynamic_vector_input();
    require(inflate_zlib(dynamic_vector, expected.size(), out), "dynamic-Huffman stream decodes");
    require(out == expected, "dynamic-Huffman content");

    std::vector<std::uint8_t> large(200000);
    for (std::size_t i = 0; i < large.size(); ++i)
        large[i] = static_cast<std::uint8_t>(i * 31U + (i >> 9U));
    require(inflate_zlib(stored_zlib(large), large.size(), out) && out == large, "multi-block stored stream");

    // Damage is refused and leaves the destination cleared.
    std::vector<std::uint8_t> corrupt = dynamic_vector;
    corrupt[corrupt.size() - 1] ^= 1U;
    require(!inflate_zlib(corrupt, expected.size(), out) && out.empty(), "a wrong Adler-32 is refused");
    corrupt = dynamic_vector;
    corrupt[400] ^= 0x5AU;
    require(!inflate_zlib(corrupt, expected.size(), out), "a damaged body is refused");
    const std::vector<std::uint8_t> truncated(dynamic_vector.begin(), dynamic_vector.begin() + 900);
    require(!inflate_zlib(truncated, expected.size(), out), "a truncated stream is refused");
    require(!inflate_zlib(dynamic_vector, expected.size() - 1, out), "a wrong expected size is refused");
}

// ------------------------------------------------------------------ rasterization

struct CountingPolicy {
    static constexpr bool perspective = true;
    int width{};
    std::vector<int>* counts{};
    int fronts{};
    int backs{};
    bool test(int, int, float) {
        return true;
    }
    void cover(const Coverage& c, bool front) {
        ++(*counts)[static_cast<std::size_t>(c.y * width + c.x)];
        require(std::abs(c.weight0 + c.weight1 + c.weight2 - 1.0F) < 1e-3F, "weights sum to one");
        if (front)
            ++fronts;
        else
            ++backs;
    }
};

Projection flat_projection(int size) {
    // Camera at the origin looking down -z; a point at depth 1 maps x = +-1 to the edges.
    CameraSetup camera{};
    camera.eye = {0, 0, 0};
    camera.target = {0, 0, -1};
    camera.field_of_view_degrees = 90;
    camera.near_plane = 0.1F;
    Projection projection = make_projection(camera, size, size);
    return projection;
}

void test_raster() {
    const int size = 64;
    const Projection projection = flat_projection(size);
    std::vector<int> counts(static_cast<std::size_t>(size * size), 0);
    CountingPolicy policy{size, &counts};
    // A fan of eight triangles around an off-grid centre, all sharing edges: every
    // covered pixel must be covered exactly once.
    const Vec3 centre{0.137F, -0.091F, -1.0F};
    const int sides = 8;
    for (int k = 0; k < sides; ++k) {
        const float a0 = 6.2831853F * static_cast<float>(k) / sides + 0.21F;
        const float a1 = 6.2831853F * static_cast<float>(k + 1) / sides + 0.21F;
        const Vec3 p0{0.8F * std::cos(a0), 0.8F * std::sin(a0), -1.0F};
        const Vec3 p1{0.8F * std::cos(a1), 0.8F * std::sin(a1), -1.0F};
        rasterize(policy, projection, to_view(projection, centre), to_view(projection, p0), to_view(projection, p1),
                  Cull::none);
    }
    int covered = 0;
    for (const int count : counts) {
        require(count <= 1, "shared edges are covered exactly once");
        covered += count;
    }
    const double expected = 0.5 * sides * std::sin(6.2831853 / sides) * (0.8 * 32) * (0.8 * 32);
    require(std::abs(covered - expected) < expected * 0.04, "coverage matches the polygon's area");
    require(policy.fronts == covered && policy.backs == 0, "counter-clockwise triangles face the camera");

    // Reversed winding is a back face: culled on request, drawn and reported otherwise.
    std::fill(counts.begin(), counts.end(), 0);
    CountingPolicy reversed{size, &counts};
    const Vec3 a{-0.5F, -0.5F, -1.0F};
    const Vec3 b{0.5F, -0.5F, -1.0F};
    const Vec3 c{0.0F, 0.5F, -1.0F};
    rasterize(reversed, projection, to_view(projection, a), to_view(projection, c), to_view(projection, b), Cull::back);
    require(reversed.fronts + reversed.backs == 0, "back faces are culled");
    rasterize(reversed, projection, to_view(projection, a), to_view(projection, c), to_view(projection, b), Cull::none);
    require(reversed.backs > 0 && reversed.fronts == 0, "back faces are reported when not culled");

    // A triangle reaching behind the camera is clipped at the near plane, not dropped.
    std::fill(counts.begin(), counts.end(), 0);
    CountingPolicy clipped{size, &counts};
    rasterize(clipped, projection, to_view(projection, {-0.3F, -0.3F, -0.5F}), to_view(projection, {0.3F, -0.3F, -0.5F}),
              to_view(projection, {0.0F, -0.3F, 3.0F}), Cull::none);
    require(clipped.fronts + clipped.backs > 0, "a triangle crossing the near plane is clipped and drawn");
}

// ------------------------------------------------------------------ archives

void put(std::vector<std::uint8_t>& out, const void* data, std::size_t bytes) {
    const std::uint8_t* p = static_cast<const std::uint8_t*>(data);
    out.insert(out.end(), p, p + bytes);
}
void put_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    put(out, &value, 4);
}
void put_tag(std::vector<std::uint8_t>& out, const char* tag, std::uint32_t count) {
    put(out, tag, 4);
    put_u32(out, count);
}
template <typename Vertex> void put_mesh(std::vector<std::uint8_t>& out, const Mesh<Vertex>& mesh) {
    put_u32(out, static_cast<std::uint32_t>(mesh.vertices.size()));
    put_u32(out, static_cast<std::uint32_t>(mesh.indices.size()));
    put(out, mesh.vertices.data(), mesh.vertices.size() * sizeof(Vertex));
    put(out, mesh.indices.data(), mesh.indices.size() * 4);
}

// A small scene: a ground quad, one swaying blade, one fish-like creature.
SceneData synthetic_scene() {
    SceneData scene{};
    scene.camera.eye = {0, 2, 8};
    scene.camera.target = {0, 1, 0};
    scene.camera.field_of_view_degrees = 40;
    scene.camera.light = normalize({0, 1, 0.4F});
    Mesh<StaticVertex> ground{};
    const float corners[4][2] = {{-4, -4}, {4, -4}, {4, 4}, {-4, 4}};
    for (const float* corner : corners) {
        StaticVertex v{};
        v.position[0] = corner[0];
        v.position[2] = corner[1];
        v.normal[1] = 127;
        v.uv[0] = corner[0];
        v.uv[1] = corner[1];
        v.albedo[0] = 200;
        v.albedo[1] = 180;
        v.albedo[2] = 120;
        ground.vertices.push_back(v);
    }
    ground.indices = {0, 2, 1, 0, 3, 2};
    scene.static_meshes.push_back(ground);
    StaticPlacementRecord placement{};
    placement.transform[0] = 1;
    placement.transform[5] = 1;
    placement.transform[10] = 1;
    placement.tint[0] = placement.tint[1] = placement.tint[2] = 1;
    placement.uv_scale[0] = placement.uv_scale[1] = 1;
    placement.material = 7;
    scene.placements.push_back(placement);
    scene.sway_roots.push_back({0.5F, 0, 1});
    for (int row = 0; row <= 6; ++row) {
        for (int side = 0; side < 2; ++side) {
            SwayVertex v{};
            v.position[0] = 0.5F + (side == 0 ? -0.08F : 0.08F);
            v.position[1] = static_cast<float>(row) * 0.5F;
            v.position[2] = 1;
            v.normal[2] = 127;
            v.uv[0] = side == 0 ? 0 : 65535;
            v.albedo[1] = 160;
            v.compliance = 200;
            v.bend[0] = 127;
            v.along[1] = 127;
            v.distance = static_cast<float>(row) * 0.5F;
            scene.sway.vertices.push_back(v);
        }
    }
    for (std::uint32_t row = 0; row < 6; ++row) {
        const std::uint32_t a = row * 2;
        scene.sway.indices.insert(scene.sway.indices.end(), {a, a + 2, a + 1, a + 1, a + 2, a + 3});
    }
    Mesh<ActorVertex> body{};
    const float shape[4][3] = {{-0.3F, 0, 0}, {0.3F, 0, 0}, {0, 0.15F, 0}, {0, -0.15F, 0}};
    for (const float* p : shape) {
        ActorVertex v{};
        v.position[0] = p[0];
        v.position[1] = p[1];
        v.normal[2] = 127;
        body.vertices.push_back(v);
    }
    body.indices = {0, 1, 2, 0, 3, 1};
    scene.actor_meshes.push_back(body);
    ActorPartRecord part{};
    part.transform[0] = part.transform[5] = part.transform[10] = 1;
    part.tint[0] = 1;
    part.material = 11;
    part.actor = 0;
    scene.parts.push_back(part);
    ActorRecord actor{};
    actor.center[1] = 1.5F;
    actor.center[3] = 1;
    actor.cruise[0] = 1.5F;
    actor.cruise[1] = 0.2F;
    actor.cruise[2] = 0.3F;
    scene.actors.push_back(actor);
    RiserRecord riser{};
    riser.base[0] = -1;
    riser.size = 0.05F;
    riser.speed = 1;
    riser.height = 3;
    scene.risers.push_back(riser);
    return scene;
}

std::vector<std::uint8_t> encode_archive(const SceneData& scene) {
    std::vector<std::uint8_t> payload{};
    put_tag(payload, "CAMR", 1);
    const float camera[12] = {scene.camera.eye.x,    scene.camera.eye.y,    scene.camera.eye.z,
                              scene.camera.target.x, scene.camera.target.y, scene.camera.target.z,
                              scene.camera.field_of_view_degrees, 0.1F, 100, scene.camera.light.x,
                              scene.camera.light.y,  scene.camera.light.z};
    put(payload, camera, sizeof(camera));
    put_tag(payload, "TEXT", 1);
    const std::uint16_t side[2] = {4, 4};
    put(payload, side, 4);
    put_u32(payload, 0);
    const std::vector<std::uint8_t> pixels(4 * 4 * 3, 128);
    put(payload, pixels.data(), pixels.size());
    put_tag(payload, "STAT", static_cast<std::uint32_t>(scene.static_meshes.size()));
    for (const Mesh<StaticVertex>& mesh : scene.static_meshes)
        put_mesh(payload, mesh);
    put_u32(payload, static_cast<std::uint32_t>(scene.placements.size()));
    put(payload, scene.placements.data(), scene.placements.size() * sizeof(StaticPlacementRecord));
    put_tag(payload, "SWAY", 1);
    put_u32(payload, static_cast<std::uint32_t>(scene.sway_roots.size()));
    for (const Vec3& root : scene.sway_roots) {
        const float values[3] = {root.x, root.y, root.z};
        put(payload, values, sizeof(values));
    }
    put_mesh(payload, scene.sway);
    put_tag(payload, "AMSH", static_cast<std::uint32_t>(scene.actor_meshes.size()));
    for (const Mesh<ActorVertex>& mesh : scene.actor_meshes)
        put_mesh(payload, mesh);
    put_tag(payload, "APRT", static_cast<std::uint32_t>(scene.parts.size()));
    put(payload, scene.parts.data(), scene.parts.size() * sizeof(ActorPartRecord));
    put_tag(payload, "ACTR", static_cast<std::uint32_t>(scene.actors.size()));
    put(payload, scene.actors.data(), scene.actors.size() * sizeof(ActorRecord));
    put_tag(payload, "RISE", static_cast<std::uint32_t>(scene.risers.size()));
    put(payload, scene.risers.data(), scene.risers.size() * sizeof(RiserRecord));
    const std::vector<std::uint8_t> compressed = stored_zlib(payload);
    std::vector<std::uint8_t> file{};
    put(file, "AMBSCN\0\1", 8);
    put_u32(file, 1);
    put_u32(file, static_cast<std::uint32_t>(payload.size()));
    put_u32(file, static_cast<std::uint32_t>(compressed.size()));
    put_u32(file, crc32(payload));
    const std::uint8_t reserved[8]{};
    put(file, reserved, 8);
    file.insert(file.end(), compressed.begin(), compressed.end());
    return file;
}

void test_archive() {
    const SceneData source = synthetic_scene();
    const std::vector<std::uint8_t> file = encode_archive(source);
    SceneData scene{};
    std::string error{};
    require(parse_scene(file, scene, error) && error.empty(), "a well-formed archive parses");
    require(scene.sway.vertices.size() == source.sway.vertices.size() && scene.parts.size() == 1 &&
                scene.textures.size() == 1 && scene.risers.size() == 1,
            "every section arrives");

    // Damage of each kind is refused and leaves the previous scene untouched.
    const std::size_t sway_count = scene.sway.vertices.size();
    std::vector<std::uint8_t> damaged = file;
    damaged[20] ^= 1U;  // CRC
    require(!parse_scene(damaged, scene, error) && !error.empty(), "a wrong checksum is refused");
    require(scene.sway.vertices.size() == sway_count, "a refused archive leaves the scene unchanged");
    damaged = file;
    damaged.resize(damaged.size() - 10);
    require(!parse_scene(damaged, scene, error), "a truncated archive is refused");
    damaged = file;
    damaged[0] = 'X';
    require(!parse_scene(damaged, scene, error), "a wrong magic is refused");

    SceneData bad = source;
    bad.sway.indices[3] = 999;
    require(!parse_scene(encode_archive(bad), scene, error), "an index past the vertices is refused");
    bad = source;
    bad.parts[0].actor = 7;
    require(!parse_scene(encode_archive(bad), scene, error), "a part naming a missing creature is refused");
    bad = source;
    bad.sway.vertices[2].position[1] = std::nanf("");
    require(!parse_scene(encode_archive(bad), scene, error), "a non-finite position is refused");
    bad = source;
    bad.placements[0].mesh = 3;
    require(!parse_scene(encode_archive(bad), scene, error), "a placement naming a missing mesh is refused");
    require(scene.sway.vertices.size() == sway_count, "the scene survives every refusal");
}

// ------------------------------------------------------------------ motion

double distance(Point3 a, Point3 b) {
    const double result = std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
    return result;
}

void test_motion() {
    const SceneData scene = synthetic_scene();
    std::vector<Creature> creatures = creatures_from(scene.actors);
    const Pose early = creature_pose(creatures[0], 3.0);
    require(distance(early.position, creature_pose(creatures[0], 3.0).position) == 0, "poses are pure functions of time");
    // Startle a creature by tapping at its projected position.
    const Projection projection = make_projection(scene.camera, 320, 200);
    const double t = 4.0;
    const Pose before = creature_pose(creatures[0], t);
    const ScreenPoint at = to_screen(projection, to_view(projection, {static_cast<float>(before.position.x),
                                                                     static_cast<float>(before.position.y),
                                                                     static_cast<float>(before.position.z)}));
    const ScreenTap tap{at.x / 320.0 + 0.01, at.y / 200.0, 1.6, 0.2};
    require(startle(creatures, projection, tap, t, Bounds{}) == 1, "a tap beside a creature startles it");
    require(distance(creature_pose(creatures[0], t).position, before.position) < 1e-9, "the escape starts where it was");
    const Creature& fled = creatures[0];
    const double after = t + fled.escape_duration;
    require(distance(creature_pose(fled, after).position, fled.escape_to) < 1e-6, "the escape ends at its target");
    require(distance(creature_pose(fled, after + 1e-4).position, fled.escape_to) < 1e-3,
            "the new loop continues from the escape point");
    require(fled.escape_to.x < before.position.x, "a tap to the right sends it left");
    const ScreenTap far{0.02, 0.98, 1.6, 0.2};
    require(startle(creatures, projection, far, t + 5, Bounds{}) == 0, "a distant tap startles nothing");

    // The current's reach bounds every displacement it produces.
    std::vector<CurrentRoot> roots{};
    std::vector<CurrentVertex> vertices{};
    prepare_current(scene, roots, vertices);
    for (double time = 0; time < 400; time += 0.37) {
        update_current_roots(scene.sway_roots, time, roots);
        for (std::size_t index = 0; index < vertices.size(); ++index) {
            const Sway sway = current_sway(roots[scene.sway.vertices[index].root], vertices[index]);
            require(std::abs(sway.amount) <= current_reach(vertices[index]) + 1e-5F, "the sway stays within its reach");
        }
    }
}

// ------------------------------------------------------------------ cadence, presentation, settings

void test_cadence() {
    Governor governor{};
    require(governor.rates().frames == 24 && governor.rates().sway == 8, "unmeasured work runs at preferred rates");
    for (int k = 0; k < 20; ++k) {
        governor.record_frame(0.001);
        governor.record_sway(0.004);
    }
    require(governor.rates().frames == 24 && governor.rates().sway == 8, "cheap work keeps preferred rates");
    Governor slow{};
    for (int k = 0; k < 20; ++k) {
        slow.record_frame(0.002);
        slow.record_sway(0.012);
    }
    const Rates rates = slow.rates();
    require(rates.frames == 24 && rates.sway < 8 && rates.sway >= 3, "foliage slows first");
    require(slow.load() <= 0.12 + 1e-9, "the load fits the budget");
    Governor heavy{};
    for (int k = 0; k < 20; ++k) {
        heavy.record_frame(0.02);
        heavy.record_sway(0.08);
    }
    require(heavy.rates().frames == 12 && heavy.rates().sway == 3, "an overloaded scene runs at its floors");
    heavy.reset();
    require(heavy.rates().frames == 24, "reset forgets measurements");
    heavy.record_frame(std::nan(""));
    require(heavy.rates().frames == 24, "nonsense measurements are ignored");
}

void test_presentation() {
    const SceneSize retina = scene_size(2360, 1520, 2.0, Detail::balanced);
    require(retina.width == 590 && retina.height == 380, "balanced: four device pixels per scene pixel at scale 2");
    const SceneSize huge = scene_size(5120, 2880, 2.0, Detail::balanced);
    require(static_cast<double>(huge.width) * huge.height <= 262000, "the pixel budget caps a full-screen scene");
    const SceneSize fine = scene_size(1180, 760, 1.0, Detail::fine);
    const SceneSize light = scene_size(1180, 760, 1.0, Detail::light);
    require(fine.width > retina.width / 2 && light.width < fine.width, "detail orders the raster size");
    require(scene_size(0, 10, 1, Detail::fine).width == 0, "an empty view has no scene");

    const int sw = 3;
    const int sh = 2;
    const std::vector<std::uint32_t> scene{1, 2, 3, 4, 5, 6};
    std::vector<std::uint32_t> columns{};
    build_column_map(sw, 7, columns);
    std::vector<std::uint32_t> device(7 * 5, 0);
    present_nearest(scene.data(), sw, sh, columns, reinterpret_cast<std::byte*>(device.data()), 7 * 4, 7, 5);
    require(device[0] == 1 && device[6] == 3 && device[34] == 6, "corners map to corners");
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 7; ++x)
            require(device[static_cast<std::size_t>(y * 7 + x)] ==
                        scene[static_cast<std::size_t>((y * sh / 5) * sw + x * sw / 7)],
                    "nearest sampling");
}

void test_settings() {
    Settings settings{true, Detail::fine};
    Settings decoded{};
    require(decode_settings(encode_settings(settings), decoded) && decoded.paused && decoded.detail == Detail::fine,
            "settings round-trip");
    Settings untouched{true, Detail::light};
    require(!decode_settings("garbage", untouched) && untouched.detail == Detail::light, "damage keeps the old settings");
    require(decode_settings("ambient-settings 1\nfuture 9\ndetail weird\n", decoded) && !decoded.paused &&
                decoded.detail == Detail::balanced,
            "unknown keys and values fall back to defaults");
}

// ------------------------------------------------------------------ the stage, end to end

class FlatLook final : public Look {
  public:
    Rgb background(float, float v) const override {
        return {0.0F, 0.1F, 0.2F + 0.1F * v};
    }
    LightFrame light_frame() const override {
        LightFrame frame{};
        frame.right = {1, 0, 0};
        frame.up = {0, 0, -1};
        frame.toward = {0, 1, 0};
        frame.origin = {0, 10, 0};
        frame.half_width = 6;
        frame.half_height = 6;
        return frame;
    }
    FixedShade shade_fixed(const FixedSample& sample, const SceneData&) const override {
        FixedShade shade{};
        shade.color = scale(sample.albedo, 0.3F + 0.7F * sample.light_visibility);
        shade.light_boost = {0.2F, 0.2F, 0.1F};
        return shade;
    }
    float animated_light(float x, float, float time) const override {
        return 0.5F + 0.5F * std::sin(x * 3 + time);
    }
    SwayShade shade_sway(const SwaySample& sample) const override {
        SwayShade shade{};
        shade.front = sample.albedo;
        shade.back = scale(sample.albedo, 0.5F);
        shade.alpha = 1;
        return shade;
    }
    void deform_actor(std::uint32_t, const ActorVertex&, float, float, Vec3&, Vec3&) const override {}
    bool closed_actor(std::uint32_t) const override {
        return false;
    }
    Rgb shade_actor(const ActorSample&, float& alpha) const override {
        alpha = 1;
        return {1, 0, 0};
    }
    Rgb shade_riser(Vec3, Vec3, float) const override {
        return {1, 1, 1};
    }
    float joint_lift(float, float, float) const override {
        return 0;
    }
    bool closed_fixed(std::uint32_t) const override {
        return false;
    }
};

void test_stage() {
    const SceneData scene = synthetic_scene();
    const FlatLook look{};
    const ShadowMap shadows = build_shadow_map(scene, look, 128);
    const Foliage foliage = prepare_foliage(scene, look, shadows);
    require(foliage.order.size() == scene.sway.indices.size() / 3, "every foliage triangle is ordered");
    FixedLayer layer = build_fixed_layer(scene, look, shadows, foliage, 160, 100, 2, 0.5F);
    require(layer.width == 160 && layer.height == 100 && layer.settled.size() == foliage.order.size(),
            "the fixed layer has the requested size");
    std::size_t lit = 0;
    for (const std::uint32_t boost : layer.boost)
        lit += boost != 0 ? 1 : 0;
    require(lit > 1000, "the ground takes animated light");

    // A pure function of its inputs: the same layer twice, byte for byte.
    const FixedLayer again = build_fixed_layer(scene, look, shadows, foliage, 160, 100, 2, 0.5F);
    require(again.color == layer.color && again.depth == layer.depth, "fixed layers are deterministic");

    Stage stage(scene, look, foliage);
    stage.adopt(std::move(layer));
    std::vector<Creature> creatures = creatures_from(scene.actors);
    stage.update_sway(2.0);
    stage.compose(2.0, 2.0, creatures);
    const std::vector<std::uint32_t> first = stage.frame();
    int red = 0;
    for (const std::uint32_t pixel : first) {
        require((pixel >> 24U) == 255U, "frames are opaque");
        red += pixel == 0xFFFF0000U ? 1 : 0;
    }
    require(red > 10, "the creature is drawn");
    stage.compose(2.0, 2.0, creatures);
    require(stage.frame() == first, "composing the same time twice gives the same frame");
    stage.compose(2.5, 2.0, creatures);
    require(stage.frame() != first, "creatures move with time");
    stage.compose(2.0, 3.0, creatures);
    require(stage.frame() != first, "the animated light moves with its own clock");
    require(stage.counters().sway_updates == 1 && stage.counters().frames == 4, "work is counted");
}

} // namespace

int main() {
    test_inflate();
    test_raster();
    test_archive();
    test_motion();
    test_cadence();
    test_presentation();
    test_settings();
    test_stage();
    std::printf("ambient engine: %d checks passed\n", checks);
    return 0;
}
