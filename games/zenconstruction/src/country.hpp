#pragma once
// The country round the worksite: woods on the far bank thickening into forest, a meadow
// behind the gravel bar with a hedgerow across it, reeds, rushes and flowers along the
// water, ferns on the woodland floor, mossy boulders, a driftwood log, a fallen one and a
// plank laid across the brook, hills fading into the haze all round, and a sky with
// fair-weather cloud. All of it stands still, so it is drawn into the kept picture of the
// scenery (Site) and costs nothing while the camera stays put.
#include "platform/render.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace zc {

class Country {
public:
    // Makes the textures and meshes. `haze` is the colour the distance fades to (the
    // renderer's fog and the sky at the horizon); `sun` points toward the sun.
    void build(Col haze, V3 sun);
    // The landscape, drawn into the scenery picture. A tree round the eye, or between it
    // and the work, is left out so the work is never hidden.
    void draw(R3D& r) const;
    // The sky, into every pixel nothing has been drawn in.
    void draw_sky(R3D& r) const;

private:
    // A tree or a shrub: its runs of vertices in wood_ and leaves_, and the upright
    // capsule round it that decides whether it is in the way.
    struct Tree {
        std::size_t wood_first = 0, wood_count = 0;
        std::size_t leaf_first = 0, leaf_count = 0;
        V3 foot, top;
        double radius = 0;
        double bearing = 0;   // from the work, so that the trees in view run together
    };
    std::vector<Tree> trees_;
    std::vector<Vtx> wood_, leaves_;   // the trees' trunks and branches, their crowns
    std::vector<Vtx> timber_;          // the driftwood, the fallen log and the plank
    std::vector<Vtx> boulders_;
    std::vector<Vtx> shade_;           // the trees' shadows on the ground, translucent
    std::vector<Vtx> plants_;          // crossed cards cut out of plant_tex_
    std::vector<Vtx> hills_;           // three rings of hills, coloured through the haze
    Tex leaf_tex_, bark_tex_, plant_tex_;
    std::vector<float> clouds_;        // cloud cover over one repeat of the sky, row-major
    Col haze_{0.8f, 0.86f, 0.9f, 1};
    V3 sun_{0, 0, 1};
    bool autumn_ = true;   // early autumn: trees turning, as the leaves on the brook say

    void build_trees();
    void build_plants();
    void build_rocks_and_timber();
    void build_hills();
    void add_broadleaf(V3 foot, double height, double crown, std::uint32_t seed, int detail, int kind);
    void add_conifer(V3 foot, double height, double radius, std::uint32_t seed, int detail);
    void add_shrub(V3 foot, double size, std::uint32_t seed);
    void add_crown_blob(V3 centre, double radius, double squash, V3 crown_centre, double crown, Col colour, std::uint32_t seed, int detail);
    void add_shade(V3 foot, double height, double radius);
    void finish_tree(Tree tree, std::size_t wood_first, std::size_t leaf_first);
};

}  // namespace zc
