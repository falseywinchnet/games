#pragma once
// The garden's soil, painted by the shared soil engine: the dug beds in each
// season's weather, and the raccoons' burrows. A burrow is a pit going down into
// darkness, cut into the bed, with a ring of fresh dark spoil round its lip and a
// fan of it thrown out over the bed on one side, a few clods and stones on the
// heap and roots showing in the far wall.
//
// Pictures are made once per season and cell size and kept (prepare); drawing
// them costs a few hundred triangles a frame. This file draws no lawn, hedge,
// pumpkin or character: the garden calls it at the right moments in its frame.
#include "level.hpp"
#include "platform/render.hpp"

#include <cstdint>
#include <vector>

namespace ct {

enum class Season;

class Ground {
public:
    // Makes the pictures for this season and the size of a cell on screen, unless
    // it already has them. Cheap when nothing changed.
    void prepare(Season season, double pixels_per_cell);

    // The beds: every floor square of the level, burrow squares with their holes cut.
    // `key` shifts the soil so each garden shows a different patch of it.
    void draw_beds(R3D& r, const Level& level, std::uint32_t key) const;
    // The spoil fans of every burrow, before any pit: a fan may reach a neighbour.
    void draw_spoil(R3D& r, const Level& level) const;
    // One burrow's pit, rim clods and roots; its raccoon is drawn after this.
    void draw_pit(R3D& r, const Level& level, int cell) const;
    // The darkness in the pit, over whatever of the raccoon is still down there.
    void shade_pit(R3D& r, const Level& level, int cell) const;

private:
    struct Fan {
        Tex picture;
        double side = 1.0;   // world units the picture spans
    };
    bool ready_ = false;
    int season_ = -1;
    int density_ = 0;          // texels per cell, the size the pictures were made for
    double repeat_ = 8.0;      // world units the bed picture spans before it repeats
    Tex bed_;
    Tex wall_;
    Fan fans_[4];              // thrown up, right, down and left (kUp..kLeft)
    Mesh rims_[4];             // the heaped lip, trodden low on the side its fan is thrown
    std::vector<Mesh> clods_;
    Col tint_{1, 1, 1, 1};     // the season's light on the ground (moonlight at night)
    Col spoil_colour_{1, 1, 1, 1};
    int fan_direction(const Level& level, int cell) const;
    V3 centre(const Level& level, int cell) const;
};

}  // namespace ct
