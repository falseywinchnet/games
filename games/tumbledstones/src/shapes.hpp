#pragma once
// The stones' shapes. A family is one solid with two faces: an outline in the tray's plane
// and two reliefs over it, the face that lies up and the face that lies down, joined round
// a rounded edge. Each comes in a style (a shell, a spiral, a chip of petrified wood, a
// twist of pulled candy, a worry stone, a bean, a folded pillow, a faceted tumble, a
// banded agate nodule) with
// its own measures drawn from the family's seed, so no two families are alike and every
// stone of a family is the same solid.
//
// A family must show which way up and which way round it lies: no two of its eight
// orientations may look alike, though some families are made nearly so (a twist that
// looks much the same from either side, a chip that reads almost the same turned end for
// end). `orientation_distance` measures that from the faces seen from above.
//
// Units: the outline fills a socket of half-side 1 (so reaches up to `reach` on its
// diagonals). x right, y up the screen, z towards the eye.
#include <array>
#include <cstdint>
#include <vector>

namespace ts {

constexpr double reach = 1.36;
// How far a socket's floor reaches at `angle` (radians), in the shapes' units: a rounded
// square every shape is scaled to fill.
[[nodiscard]] double socket_reach(double angle);

// The least a family's orientations may differ (self_distance): nearly alike, never alike.
constexpr double least_self_distance = .06;  // the farthest any outline reaches from the middle

enum class Style { pebble, shell, spiral, wood, candy, bean, pillow, facet, nodule };
constexpr int style_count = 9;

[[nodiscard]] const char* style_name(Style style);

struct ShapeVertex {
    float x = 0, y = 0, z = 0;     // object space
    float nx = 0, ny = 0, nz = 0;  // unit normal
};

// A solid given by its density: negative inside, positive outside, about the distance to
// its surface. The grown and broken shapes are made from these (wood_volume.cpp).
struct Solid {
    virtual ~Solid() = default;
    [[nodiscard]] virtual double density(double x, double y, double z) const = 0;
};

class Shape {
public:
    static constexpr int sectors = 256;  // round the outline
    static constexpr int rings = 56;     // from the middle to the edge, closer near the edge

    // The family `seed` in `style`. `likeness` (0..1) and `parent` make it a near
    // relation of another family's measures: harder to tell apart, never identical.
    static Shape make(std::uint32_t seed, Style style, double likeness = 0,
                      const Shape* parent = nullptr);

    [[nodiscard]] std::uint32_t seed() const {
        return seed_;
    }
    [[nodiscard]] Style style() const {
        return style_;
    }
    // The solid's surface, three indices a triangle, outward winding.
    [[nodiscard]] const std::vector<ShapeVertex>& vertices() const {
        return vertices_;
    }
    [[nodiscard]] const std::vector<std::uint32_t>& triangles() const {
        return triangles_;
    }
    // The height of face `face` (0 up, 1 down) above the middle plane at object (x, y),
    // and whether (x, y) is inside the outline. Face 1's height is measured downwards.
    [[nodiscard]] bool height(int face, double x, double y, double& out) const;
    // How high the solid stands above the middle plane on each face's side: lying with
    // face f up, it rests on the other face, `depth(1 - f)` below the middle plane.
    [[nodiscard]] double depth(int face) const {
        return face == 0 ? top_peak_ : bottom_peak_;
    }

    // What the eye sees of orientation `o` (face o / 4 up, turned (o % 4) quarter turns
    // clockwise): heights on a `side` x `side` grid over [-1, 1]^2, row 0 at the top,
    // -0.35 outside the outline. The grid spans [-reach, reach]^2.
    [[nodiscard]] std::vector<float> seen(int orientation, int side) const;

    // Fine relief too small for the solid's mesh (hairline cracks, fibres, pits, the ridges
    // of agate's bands) at object (x, y) on a face: its slopes, and how hollow it lies
    // (0..1, for the light it keeps out). False where the family has none.
    [[nodiscard]] bool fine(int face, double x, double y, float& slope_x, float& slope_y, float& hollow) const;

    // An agate nodule's bands at a point of the solid, counted from its centre (-1 for
    // other styles): a banded mineral follows them, as agate's colour follows its growth.
    [[nodiscard]] double bands(double x, double y, double z) const;
    // The direction a grained material's grain runs in the stone (object space): along a
    // piece of fossil log, or straight up through a round of log stood on its end.
    void grain_axis(double out[3]) const;
    // A point on the line the grain grows round (the pith), object space.
    void grain_centre(double out[3]) const;

    // The parameters the family was drawn with, for its relations.
    struct Measures {
        double aspect = 1, turn = 0;              // outline stretch along x; its turn
        std::array<double, 6> lobe{};             // outline wobble, harmonics 2..7
        std::array<double, 6> lobe_phase{};
        double mark_angle = 0, mark_width = .3, mark_depth = 0;  // a notch or bulge
        double top = .4, bottom = .3;             // the faces' heights
        double top_power = 2, bottom_power = 2;   // their edge profile: 2 round, more flat
        std::array<double, 16> feature{};         // the style's own measures
    };
    [[nodiscard]] const Measures& measures() const {
        return measures_;
    }

private:
    std::uint32_t seed_ = 0;
    Style style_ = Style::pebble;
    Measures measures_;
    std::vector<float> outline_;     // sectors radii
    std::vector<float> ring_u_;      // rings + 1 fractions of the outline, 0 at the middle
    std::vector<float> top_;         // (rings + 1) x sectors heights, ring-major
    std::vector<float> bottom_;
    double top_peak_ = 0, bottom_peak_ = 0;
    double scale_ = 1;               // the measures' units to the socket-filling outline
    bool grown_ = false;             // made as a solid (wood_volume.cpp)
    double grain_[3] = {1, 0, 0};    // a grown piece's grain, object space
    double pith_[3] = {0, 0, 0};     // and a point of its pith
    std::vector<ShapeVertex> vertices_;
    std::vector<std::uint32_t> triangles_;
    struct Detail {
        int side = 0;                       // texels across [-reach, reach]
        std::vector<float> slope_x, slope_y, hollow;
    };
    Detail detail_[2];

    static Shape make_once(std::uint32_t seed, Style style, double likeness, const Shape* parent, int attempt);
    void build(std::uint64_t& random);
    void sample();
    void grow_wood();               // wood_volume.cpp: a piece of fossil log, grown then broken
    void grow_shell();              // shell_volume.cpp: a shell, a shard or a scalloped rock, broken
    // A solid made into this shape: found in a box of half-side `box` round `middle`, laid
    // its thinnest way up, meshed, turned and scaled to the socket. `grain` and `pith` (the
    // solid's frame) place a grained material's grain.
    void grow_from(const Solid& solid, const double middle[3], double box, const double grain[3], const double pith[3]);
    void mesh();
    [[nodiscard]] double outline_at(double angle) const;
    // An outline found by marching (an agate nodule's rounded polygon): whether a point (in
    // the unturned frame) is inside, and the farthest reach along a ray.
    [[nodiscard]] bool nodule_inside(double x, double y) const;
    [[nodiscard]] double reach_inward(double c, double s) const;
    [[nodiscard]] double hashed(int a, int b) const;
    [[nodiscard]] double feature_height(int face, double x, double y, double u) const;
    [[nodiscard]] double fine_height(int face, double x, double y) const;
    [[nodiscard]] double band_field(double x, double y, double z) const;
    [[nodiscard]] double wear(int face, double x, double y) const;
    void bake_detail();
};

// How unlike two seen faces are: the mean difference of heights and slopes, with the
// outline counting fully. 0 only for the same picture.
[[nodiscard]] double seen_distance(const std::vector<float>& a, const std::vector<float>& b, int side);

// The least distance between any two of a family's eight orientations: how carefully the
// player must look.
[[nodiscard]] double self_distance(const Shape& shape, int side = 32);
// The least distance between any orientation of one family and any of another.
[[nodiscard]] double cross_distance(const Shape& a, const Shape& b, int side = 32);

}  // namespace ts
