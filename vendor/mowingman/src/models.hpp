#pragma once
// The solid models, shared between the model files. art.hpp's draw functions for the
// mower, gnome, old lady, fountain, lounger and tree trunks are implemented with them.
#include "model3d.hpp"

#include <memory>
#include <mutex>

namespace mm {

// Meshes are made for a scale: the nearest step at or above the frame's pixels per
// metre, in steps of a half octave, so a window being resized does not remake them.
double detail_for(double ppm);

// A thing that does not move, kept as a picture for as long as the frame is the same.
struct StillKey {
    int kind{};
    std::uint64_t seed{};
    long long ppm{};
    long long ox{};
    long long oy{};
    [[nodiscard]] bool same(const StillKey& o) const {
        return kind == o.kind && seed == o.seed && ppm == o.ppm && ox == o.ox && oy == o.oy;
    }
};
StillKey still_key(int kind, std::uint64_t seed, const Frame& frame);
// The kept picture for `key`, or null.
const Shot* find_still(const StillKey& key);
const Shot& keep_still(const StillKey& key, Shot shot);

// Material colours given as hex, sRGB.
Material paint_material(unsigned colour, float gloss, float specular = 0.05F);

void draw_fountain_model(Canvas& canvas, const Frame& frame, const Prop& prop);
void draw_fountain_water(Canvas& canvas, const Frame& frame, const Prop& prop, double time);
void draw_birdbath_model(Canvas& canvas, const Frame& frame, const Prop& prop);
void draw_birdbath_water(Canvas& canvas, const Frame& frame, const Prop& prop, double time);
void draw_shed_model(Canvas& canvas, const Frame& frame, const Prop& prop, double heading, double hx, double hy);
void draw_lounger_model(Canvas& canvas, const Frame& frame, const Prop& prop, double heading);

} // namespace mm
