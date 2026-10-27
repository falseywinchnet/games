#pragma once
// The parlor: a long table in a white cloth, the evidence of the small crime
// laid out in the middle (crumbs on the plate, the toppled teapot, the pecked
// cake, the empty sugar jar, the shiny button's empty dish, the mirror's pale
// patch), the parrots perched in a row behind it facing you, and a cosy room
// around them: striped wallpaper, a window, a portrait of a very grand parrot.
#include "birds.hpp"
#include "platform/r3d.hpp"

#include <string>
#include <vector>

namespace pt {

struct ParlorState {
    std::vector<BirdPose> birds;
    std::string prop = "plate";
    double lamp = 1;        // 0..1 the room's warmth (dims a little in the reckoning)
    double spot = -1;       // which bird a spotlight picks out at the end (-1 none)
    double confetti = 0;    // 0..1 a celebration
};

class Parlor {
public:
    R3D r;
    void resize(int w, int h, int bottom_panel, int top_band = 0);  // the birds are framed between the top band and the panel
    void seat(int n, ParlorState& s);         // places n birds along the far side of the table, and frames them
    void render(const ParlorState& s, double t);
    void to_screen(V3 p, double& sx, double& sy) const;
    int pick_bird(double sx, double sy, const ParlorState& s) const;

private:
    int panel_ = 0, w_ = 0, h_ = 0;
    void draw_room(const ParlorState& s, double t);
    void draw_table(const ParlorState& s, double t);
};

}  // namespace pt
