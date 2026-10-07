#pragma once
// Soil: a seeded, deterministic painter of garden ground. It builds a height field
// in metres (clods and crumbs of every size, furrows, rake lines, crust split into
// plates, pebbles and grit, straw, twigs and fallen leaves), lights it with soft
// self-shadowing and crevice darkening, darkens it where it is damp, and dusts it
// with frost or snow. Surfaces repeat seamlessly in both directions; a spoil heap
// (the fan of fresh soil thrown out of a hole) is a single picture with alpha.
//
// The light is baked in: the result is a picture of the ground, meant to be shown
// unlit. A flat, open patch of soil comes out at the colour asked for, so a game
// keeps control of its palette. Generation takes milliseconds, not microseconds:
// make each picture once per scene and size and keep it. No clock, threads or
// GUI.Forms here.
#include <cstdint>
#include <vector>

namespace soil {

// An sRGB colour, each channel 0..1.
struct Colour {
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
};

struct Parameters {
    // How many texels one metre of ground spans. Every size below is in metres,
    // so the same parameters make the same ground at any resolution; detail
    // smaller than a texel becomes fine mottling instead of vanishing.
    double texels_per_metre = 160.0;

    // The soil. `colour` is a flat, open, dry patch in full light.
    Colour colour{0.50, 0.39, 0.29};
    double moisture = 0.45;          // 0 dust dry .. 1 saturated (darker, richer, a sheen)
    double moisture_patches = 0.35;  // 0 evenly damp .. 1 drying in blotches
    double tilth = 0.5;              // clod size: 0 fine crumb .. 1 coarse fresh-dug clods
    double looseness = 1.0;          // 1 freshly worked .. 0 trodden hard, as on a path
    double relief = 1.0;             // multiplies every height: 0.5 gentler, 2 rougher
    double subsoil = 0.0;            // 0..1 lumps of paler, yellower subsoil turned up with it

    // Rows. Furrows are the ridges and troughs of a dug bed; rake lines the fine
    // tine marks of a raked one. An angle of 0 runs them along +x.
    double furrow_angle = 0.0;       // radians
    double furrow_spacing = 0.0;     // metres from ridge to ridge; 0 for none
    double furrow_depth = 0.05;      // metres from ridge to trough
    double rake_angle = 0.0;         // radians
    double rake_spacing = 0.0;       // metres between tine marks; 0 for none
    double rake_depth = 0.006;       // metres

    // What lies on it, each 0 (none) .. 1 (plenty).
    double pebbles = 0.3;
    double grit = 0.4;
    double straw = 0.0;
    double twigs = 0.0;
    double leaves = 0.0;
    Colour leaf_colour{0.62, 0.34, 0.12};  // the season's fallen leaves; varied per leaf

    // Weather, each 0 .. 1.
    double cracks = 0.0;             // a dry crust split into curling plates
    double frost = 0.0;              // hoar frost on the tops of clods
    double snow = 0.0;               // a dusting of snow: tops first, then the hollows

    // The baked light. Azimuth is the direction the light comes from in the
    // picture: 0 from the right (+x), pi/2 from the bottom (+y, rows grow downward);
    // the default, from the upper left, is how people expect a picture to be lit.
    double light_azimuth = 3.93;
    double light_elevation = 0.8;    // radians above the ground
    double view_azimuth = 1.5708;    // where the viewer stands, for wet glints
    double view_elevation = 0.82;

    std::uint32_t seed = 1;
};

// Starting points for common ground. Adjust the fields from there.
enum class Preset {
    tilled_loam,     // dug over: clods of every size, a few stones, a little straw
    raked_bed,       // fine tilth combed into tine marks, ready for seed
    furrowed_rows,   // ridged rows between which seed is sown
    compacted_path,  // trodden flat and pale, grit and pebbles pressed in
    dry_crust,       // a sun-baked crust split into curling plates
    damp_dark,       // after rain: dark, rich, glistening
    leaf_litter,     // an autumn bed under fallen leaves and twigs
    frosted,         // a hard morning frost on dug soil
    snow_dusted,     // a light snow that has not yet covered the clods
    fresh_spoil      // newly thrown out of a hole: dark, moist, loose and crumbly
};
inline constexpr int preset_count = 10;
const char* preset_name(Preset preset);
Parameters preset(Preset preset);

enum class Season { spring, summer, autumn, winter };
// Moves parameters toward a season: damp spring, dry summer with a few cracks,
// autumn leaves, a frosted and snow-dusted winter. Keeps colour, scale and rows.
void apply_season(Parameters& parameters, Season season);

// A picture of ground: straight-alpha sRGB, row-major, width * height * 4 bytes in
// the order red, green, blue, alpha. `relief` is the height field in metres
// (width * height, row-major), for a caller that wants to light or place things.
struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    std::vector<float> relief;
};

// The largest side generate_surface and generate_spoil accept.
inline constexpr int max_side = 4096;

// A surface that repeats seamlessly in both directions. Rows, cracks and noise
// are fitted to whole periods across the picture, so a requested spacing may be
// adjusted slightly. Fails (leaving `out` untouched) for sides outside 4..max_side.
[[nodiscard]] bool generate_surface(const Parameters& parameters, int width, int height,
                                    Image& out);

// The shape of a spoil heap: a raised ring of fresh soil round a hole, and a fan
// of it thrown out to one side, thinning to scattered crumbs. Lengths are metres.
struct Spoil {
    double hole_radius = 0.12;   // nothing is drawn inside; the caller draws the hole
    double rim_width = 0.07;     // the ring of soil heaped round the lip
    double rim_height = 0.035;
    double fan_direction = 1.5708;  // radians in the picture (0 +x, pi/2 +y)
    double fan_spread = 0.85;    // half-angle of the fan, radians
    double fan_reach = 0.28;     // from the lip to the last crumbs
    double fan_height = 0.045;
    double threshold = 0.5;      // 0..1 how trodden the lip is where the fan leaves the
                                 // hole: smoothed, packed and a little paler
};

// A heap of spoil centred in a width x height picture, alpha 0 where the ground
// shows through. A soft shadow it casts on the ground is included as dark,
// partly transparent pixels. Fails (leaving `out` untouched) for sides outside
// 4..max_side.
[[nodiscard]] bool generate_spoil(const Parameters& parameters, const Spoil& spoil, int width,
                                  int height, Image& out);

}  // namespace soil
