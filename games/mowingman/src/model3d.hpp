#pragma once
// Solid models for the things that stand on the lawn: the mowers, the gnome, the old
// lady, the fountain, the lounger and its sleeper, the tree trunks.
//
// The garden's Frame is an orthographic camera: the ground is foreshortened by `tilt`
// and heights by `rise`, and tilt² + rise² is 1, so the view looks down at about 52°.
// Models are triangle meshes in metres (x east, y towards the viewer, z up), shaded
// per pixel with the garden's sun (from the upper left, as every shadow in the garden
// falls), a sky and grass hemisphere, ambient occlusion baked into the vertices, a
// shadow map for the shade one part throws on another, and the shadow the whole model
// throws on the grass. Four samples a pixel give clean edges.
//
// A Shot is a rendered model held in canvas pixels with its depth, so things that
// do not move (a fountain's stone, a sleeper on a lounger) are drawn once and only
// their moving water is drawn each frame, hidden where the stone stands in front.
#include "art.hpp"
#include "platform/raster.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

namespace mm {

struct V3 {
    double x{};
    double y{};
    double z{};
};
inline V3 operator+(V3 a, V3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline V3 operator-(V3 a, V3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline V3 operator*(V3 a, double k) {
    return {a.x * k, a.y * k, a.z * k};
}
inline double dot(V3 a, V3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline V3 cross(V3 a, V3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double length(V3 a) {
    return std::sqrt(dot(a, a));
}
inline V3 normalized(V3 a) {
    const double n = length(a);
    return n > 1e-12 ? a * (1 / n) : V3{0, 0, 1};
}
inline V3 lerp(V3 a, V3 b, double t) {
    return a + (b - a) * t;
}

// An affine transform: p' = M p + t, rows of M then t in m[3], m[7], m[11].
struct Xform {
    double m[12]{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    [[nodiscard]] V3 point(V3 p) const {
        return {m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3], m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7], m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]};
    }
    [[nodiscard]] V3 vector(V3 p) const {
        return {m[0] * p.x + m[1] * p.y + m[2] * p.z, m[4] * p.x + m[5] * p.y + m[6] * p.z, m[8] * p.x + m[9] * p.y + m[10] * p.z};
    }
    // Normals go through the inverse transpose; for a scaled rotation this is its cofactor matrix.
    [[nodiscard]] V3 normal(V3 n) const;
    [[nodiscard]] Xform operator*(const Xform& o) const;  // this after o
};
Xform translation(V3 t);
Xform scaling(double sx, double sy, double sz);
Xform rotation_x(double a);
Xform rotation_y(double a);
Xform rotation_z(double a);
// The frame that carries +x along `axis` (and keeps +z as near up as it can), at `origin`.
Xform aimed(V3 origin, V3 axis);

// What a surface is made of. Colours are given as sRGB and lit in linear light.
enum class Pattern : std::uint8_t {
    none,
    tread,       // a tyre's chevron lugs; u round the tyre, v across it
    stone,       // weathered cast stone, mottled; object position
    wet_stone,   // stone darkened and mossed towards `level`
    bark,        // furrowed bark running up z
    floral,      // small flowers in `tint` on the ground colour; uv
    knit,        // knitted ribs; uv
    stripes,     // `tint` stripes across v
    tread_plate, // diamond tread plate; object x, y
    grille,      // a slotted grille: dark slots; uv
    glyph,       // a badge letter in `tint`; uv in -1..1
    scales,      // an acorn cup's scales; uv
    hair,        // combed hair or beard strands along v
    wood,        // turned wood grain along u
    down,        // a duckling's down; object position
    weave,       // basket-woven sling; uv
    water,       // rippled water; world position and time
    curtain,     // falling water, streaming down v with time
    blossom,     // tropical shirt print; uv
    skin,        // skin: a little mottling and warmth
    rubber,      // grips and seats: fine pebbling
    metal_brushed,
    cap_print,   // gnome's coat: faint brush marks of paint
    sand,        // play sand: grains of several tones, scooped hollows, damp patches; object position
    shell,       // a toy turtle's shell: hexagonal plates in `tint` with darker seams; object x, y
};

struct Material {
    Col base{};        // albedo, sRGB
    Col tint{};        // second colour, for patterns and the per-vertex blend
    float specular{0.04F};  // reflectance at normal incidence
    float gloss{0.3F};      // 0 rough .. 1 mirror
    float metal{0};         // 0 dielectric .. 1 metal (tints reflections, no diffuse)
    float wrap{0};          // light wrapping round the terminator (skin, cloth, fluff)
    float sheen{0};         // grazing-angle brightening (velvet, fluff)
    float emit{0};          // glows with its own colour
    float opacity{1};       // under 1: drawn after the solids, blended over them
    float level{0};         // pattern parameter (a water line's height, a glyph, a scale)
    float scale{1};         // pattern frequency
    bool two_sided{};       // thin sheets: lit from whichever side is seen
    Pattern pattern{Pattern::none};
};

struct Vertex {
    V3 p{};        // position
    V3 n{};        // normal
    V3 o{};        // where it was made, before placing: patterns stick to the object
    float u{};
    float v{};
    float ao{1};   // how much sky it sees, 0..1
    float blend{}; // 0 base .. 1 tint
    V3 colour{1, 1, 1};  // a linear tint the albedo is multiplied by (stones carry their colours here)
};

struct Mesh {
    std::vector<Vertex> vertices{};
    std::vector<std::uint32_t> indices{};      // three per triangle
    std::vector<std::uint16_t> materials{};    // one per triangle
    [[nodiscard]] std::size_t triangles() const {
        return materials.size();
    }
    void append(const Mesh& other, const Xform& place);
};

// Builds meshes from swept, turned and lofted shapes. `ppm` says how fine the
// curves must be: round things get enough sides that no facet is wider than a
// couple of pixels at that scale.
class Builder {
  public:
    Builder(Mesh& mesh, double ppm);
    Xform at{};          // where the next shape goes
    std::uint16_t material{};
    float blend{};
    [[nodiscard]] int sides(double radius, int least = 6, int most = 72) const;

    // A surface of revolution about z: (radius, height) pairs from bottom to top.
    void lathe(const std::vector<double>& radius, const std::vector<double>& height, int segments = 0, double a0 = 0, double a1 = 6.283185307179586);
    void ellipsoid(V3 centre, V3 radii, int segments = 0);
    void cylinder(V3 a, V3 b, double ra, double rb, bool caps = true, int segments = 0);
    void sphere(V3 centre, double r) {
        ellipsoid(centre, {r, r, r});
    }
    // A tube through points, each with its radius: limbs, rails, hoses, stalks.
    void tube(const std::vector<V3>& points, const std::vector<double>& radii, bool caps = true, int segments = 0);
    void torus(V3 centre, double major, double minor, int segments = 0, int rings = 0);
    // A box with rounded edges and corners.
    void rounded_box(V3 centre, V3 half, double r);
    // Sections across x: each a superellipse of half width w (y) and half height h (z)
    // centred at (x, cy, cz); `power` 2 is an ellipse, higher is boxier. Ends are capped.
    struct Section {
        double x{};
        double w{};
        double h{};
        double cz{};
        double power{2.5};
        double cy{};
    };
    void loft(const std::vector<Section>& sections, int around = 0, bool caps = true);
    // A superellipse section (half widths w across `side`, h across the other way) swept
    // along a path: fenders, chutes, bent straps. Sizes may change along the path.
    void sweep(const std::vector<V3>& points, const std::vector<double>& w, const std::vector<double>& h, double power, V3 side, bool caps = true, int around = 0);
    // A flat polygon (counter-clockwise in xy) extruded from z0 to z1, with straight sides.
    void prism(const std::vector<double>& xs, const std::vector<double>& ys, double z0, double z1);
    // A grid of points rows x cols, wrapped round in u if `ring`; normals from the grid.
    void grid(const std::vector<V3>& points, int rows, int cols, bool ring, bool flip = false);
    // A single quad or triangle sheet (thin things: a lens, a blade, a petal).
    void quad(V3 a, V3 b, V3 c, V3 d);

  private:
    void emit(const std::vector<Vertex>& local, int rows, int cols, bool ring, bool flip);
    Mesh& mesh_;
    double ppm_;
};

// Bakes how much of the sky each vertex sees, counting the ground as a floor.
void bake_occlusion(Mesh& mesh, int directions = 40);

struct Part {
    const Mesh* mesh{};
    const std::vector<Material>* materials{};
    Xform place{};
    bool casts_only{};  // throws shadow but is not drawn (stone shading the water)
    const Mesh* shadow{};  // a coarser mesh to throw the shadows, if there is one
};

struct ShadowMap {
    V3 r{};
    V3 u{};
    V3 l{};
    double x0{};
    double y0{};
    double cell{};
    int w{};
    int h{};
    std::vector<float> depth{};
    [[nodiscard]] bool empty() const {
        return w <= 0;
    }
    // 0 in full shade .. 1 in the sun, softened over neighbouring cells.
    [[nodiscard]] float light(V3 p, double bias) const;
};

struct Shot {
    int x0{};
    int y0{};
    int w{};
    int h{};
    std::vector<std::uint8_t> px{};  // BGRA premultiplied, w * h
    std::vector<float> depth{};      // nearest depth seen per pixel (larger is nearer), or a large negative
    int sx0{};
    int sy0{};
    int sw{};
    int sh{};
    std::vector<std::uint8_t> shadow{};  // the ground shadow's darkness, sw * sh
    ShadowMap map{};
    [[nodiscard]] bool empty() const {
        return w <= 0 || h <= 0;
    }
};

struct ShotOptions {
    double time{};
    bool clip_ground{};        // nothing below the grass (the gnome climbing out)
    bool ground_shadow{true};
    float shadow_strength{0.5F};
    const Shot* occluder{};    // already drawn: hide behind its depth and use its shadow map
};

// The depth a world point has in the picture: larger is nearer the viewer.
double view_depth(const Frame& frame, V3 p);
void shoot(const Frame& frame, const std::vector<Part>& parts, const ShotOptions& options, Shot& shot);
// Laid over the canvas, moved by whole pixels (a kept picture drawn somewhere else).
void paint_shadow(Canvas& canvas, const Shot& shot, int dx = 0, int dy = 0);
void paint(Canvas& canvas, const Shot& shot, int dx = 0, int dy = 0);

// Small helpers for the model files.
double hash01(std::uint64_t& state);
double noise3(V3 p);                  // smooth value noise, 0..1
double fbm3(V3 p, int octaves);       // 0..1

} // namespace mm
