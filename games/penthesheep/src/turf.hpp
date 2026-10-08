#pragma once
// The meadow's ground, grown blade by blade by the shared grass engine (shared/grass),
// as Mowing's lawn and Catching Thieves' field are: short mown turf on the hexagon
// patches and longer meadow grass with wildflowers in the field around them. Both are
// folded so they wrap without a seam. Growing takes some tenths of a second, so the
// pasture grows them on a worker and keeps its painted grass until they are ready.
#include "platform/r3d.hpp"

namespace sh {

struct GroundArt {
    Tex turf;   // covers kTurfUnits world units each way
    Tex field;  // covers kFieldUnits world units each way
};
inline constexpr double kTurfUnits = 2.0, kFieldUnits = 8.0;

[[nodiscard]] GroundArt grow_ground();

}  // namespace sh
