#include "models.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace mm {
namespace {

struct Still {
    StillKey key{};
    Shot shot{};
};

struct Stills {
    std::mutex guard{};
    std::vector<std::unique_ptr<Still>> kept{};
};

Stills& stills() {
    static Stills all{};
    return all;
}

} // namespace

double detail_for(double ppm) {
    const double step = std::ceil(std::log2(std::max(8.0, ppm)) * 2 - 1e-9) / 2;
    return std::pow(2.0, step);
}

StillKey still_key(int kind, std::uint64_t seed, const Frame& frame) {
    StillKey key{};
    key.kind = kind;
    key.seed = seed;
    key.ppm = std::llround(frame.ppm * 1000);
    key.ox = std::llround(frame.ox * 1000);
    key.oy = std::llround(frame.oy * 1000);
    return key;
}

const Shot* find_still(const StillKey& key) {
    Stills& all = stills();
    const std::lock_guard<std::mutex> lock(all.guard);
    for (const std::unique_ptr<Still>& still : all.kept)
        if ((*still).key.same(key))
            return &(*still).shot;
    return nullptr;
}

const Shot& keep_still(const StillKey& key, Shot shot) {
    Stills& all = stills();
    const std::lock_guard<std::mutex> lock(all.guard);
    // Pictures made for another frame are no use once the view has changed.
    std::vector<std::unique_ptr<Still>> keep{};
    for (std::unique_ptr<Still>& still : all.kept)
        if ((*still).key.ppm == key.ppm && (*still).key.ox == key.ox && (*still).key.oy == key.oy)
            keep.push_back(std::move(still));
    all.kept = std::move(keep);
    if (all.kept.size() > 64)
        all.kept.erase(all.kept.begin());
    std::unique_ptr<Still> still = std::make_unique<Still>();
    (*still).key = key;
    (*still).shot = std::move(shot);
    all.kept.push_back(std::move(still));
    return (*all.kept.back()).shot;
}

Material paint_material(unsigned colour, float gloss, float specular) {
    Material m{};
    m.base = hex(colour);
    m.tint = hex(colour);
    m.gloss = gloss;
    m.specular = specular;
    return m;
}

} // namespace mm
