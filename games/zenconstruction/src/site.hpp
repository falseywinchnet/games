#pragma once
// The worksite drawn: a shallow sand bed in timber edging, a gravel bank
// behind it and a babbling brook beyond carrying leaves and the odd twig; the
// bowl of rocks; the little yellow crane with its operator; the stack; the
// sign with the company's name. Software 3D at low resolution with sun
// shadows. When nothing on the site moves, the still picture is kept and only
// the brook is redrawn each frame.
#include "crane_model.hpp"
#include "platform/render.hpp"
#include "run.hpp"

#include <cstdint>
#include <vector>

namespace zc {

// The orbiting camera: it looks at `target` from `distance` metres, turned
// `yaw` about the vertical (0 looks north, along +y) and `pitch` down from level.
struct OrbitCamera {
    double yaw = 0.0;
    double pitch = 0.55;
    double distance = 1.85;
    phys::Vec3 target{0.02, 0.02, 0.10};
};

struct SceneState {
    const Run* run = nullptr;
    double time = 0;
    int hover_rock = -1;            // outlined: green if the crane can fetch it, red if not
    bool hover_ok = false;
    OperatorMood mood = OperatorMood::idle;
};

class Site {
public:
    Site();
    R3D r;

    void resize(int width, int height);
    void set_camera(const OrbitCamera& camera);
    // the board of the worksite sign, painted by the caller (the company's name)
    void set_sign(const Tex& board);
    // Draws the site. `still` says nothing on it is moving (the picture of
    // everything but the brook may be reused from the last still frame).
    void render(const SceneState& state, bool still);
    void invalidate() { cache_valid_ = false; }

    // the ray from the eye through screen point (sx, sy), in world space
    void ray(double sx, double sy, phys::Vec3& origin, phys::Vec3& direction) const;
    // screen position of a world point; false if it's behind the eye
    bool project(phys::Vec3 p, double& sx, double& sy) const;

    long long triangles() const { return r.tris_drawn; }
    const CraneModel& crane() const { return crane_; }

private:
    OrbitCamera camera_;
    // static scenery, in world space
    std::vector<Vtx> bank_, ledge_, cobbles_, brook_bed_, far_bank_, water_, bowl_, sign_post_, boulder_, reeds_, trees_, stream_stones_, props_, lap_;
    std::vector<V3> stream_stone_spots_;   // where foam rings go (x, y, radius)
    Tex gravel_tex_, cobble_tex_, splat_tex_, bedrock_tex_, grass_tex_, wood_tex_, bowl_tex_, water_tex_, glint_tex_, foam_tex_, sign_tex_, glow_tex_, streak_tex_;
    // the still picture
    bool cache_valid_ = false;
    std::vector<float> cache_rgb_, cache_depth_;
    OrbitCamera cached_camera_;
    int cached_w_ = 0, cached_h_ = 0;
    // working storage reused each frame
    std::vector<Vtx> scratch_;
    CraneModel crane_;
    // the kept shadow of everything that isn't moving
    bool shadow_kept_ = false;
    std::uint64_t shadow_key_ = 0;
    std::vector<char> shadow_still_;

    void build_textures();
    void build_scenery();
    void draw_sky();
    void draw_scenery();
    void draw_brook(double time);
    void draw_bowl();
    void draw_sign();
    void cast_shadows(const SceneState& state);
    void draw_rocks(const SceneState& state);
    double ground_at(double x, double y) const;   // the bank or the boulder, whichever is higher
    void place_crane();
    void build_crane(const SceneState& state);
    void draw_crane();
    void draw_slings(const SceneState& state);
};

// M34 from a physics pose
M34 pose_matrix(const phys::Pose& pose);

}  // namespace zc
