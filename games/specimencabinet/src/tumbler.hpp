#pragma once
// The tumbler: a drum of black rubber tipped with its open mouth raised over the felt, as a
// mixer's is, its body running off beyond the top of the window. Fins inside lift the
// stones as it turns and spill them over the lip. Ray cast from straight above (the
// table's own view) and lit by the stones engine's room, as the stones are, with its soft
// shadow on whatever lies beneath.
//
// It is drawn in two passes so stones can lie inside it: `body` draws its shadow and the
// drum; the caller lays the stones inside the mouth; `over` then shades them with the
// drum's dark and draws the rim and the outside over any part of them that strays.
#include "target.hpp"

namespace sc {

struct TumblerLook {
    double x = 0;        // the mouth's centre, device pixels
    double y = 0;
    double radius = 40;  // device pixels
    double turn = 0;     // how far it has turned, radians
    double scale = 2;    // device pixels a point
};

enum class TumblerPass { body, over };

void draw_tumbler(const render::Target& target, const TumblerLook& look, TumblerPass pass);
// The rows the tumbler and its shadow touch, from the top of the surface (device pixels).
[[nodiscard]] int tumbler_bottom(const TumblerLook& look);

}  // namespace sc
