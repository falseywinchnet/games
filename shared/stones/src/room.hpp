#pragma once
// The light in the room where the tray lies: the card tables' sunlit room, path traced
// once from the tray (tools/bake_room.cpp writes room_bake.cpp). A polished stone mirrors
// that room, its window, its ceiling lights, its walls, so what it shows is looked up
// here by direction: sharply for a mirror polish, blurred for gloss and satin, and as
// the light falling on a surface facing that way for its body colour.
//
// Directions are the tray's: x right, y up the screen (away from the player), z up.
// Values are linear light; `exposure()` brings white paper facing up to the screen's
// own white, as the card tables' cards are.
#include <vector>

namespace stones {

struct Light3 {
    float r = 0, g = 0, b = 0;
};

// The room mirrored in a surface whose reflection runs along (x, y, z), for a polish of
// `roughness` (0 a mirror, 1 satin).
[[nodiscard]] Light3 room_reflection(double x, double y, double z, double roughness);
// The light a diffuse surface facing (x, y, z) receives, over pi (its radiance for an
// albedo of one), without the sun's disc.
[[nodiscard]] Light3 room_diffuse(double x, double y, double z);

// The sun: its direction, and the light it brings to a surface facing it (zero in a room
// where it does not reach the tray).
struct Sun {
    double x = 0, y = 0, z = 1;
    Light3 light;
};
[[nodiscard]] const Sun& room_sun();

// The room's light gathered into a few directions over the upper half, each with its
// share of what falls on a surface facing up: what a stone or the tray's rim can shade.
struct LightDirection {
    double x = 0, y = 0, z = 1;
    double weight = 0;
};
[[nodiscard]] const std::vector<LightDirection>& room_lights();

// Linear light to the screen's scale: white paper facing up to 0.85.
[[nodiscard]] double exposure();

// Linear light, exposed, to a screen value 0..1: linear to 0.75, then a soft shoulder so
// a window's glint does not clip flat; sRGB coded.
[[nodiscard]] float screen_value(double light);

// The bake (room_bake.cpp, generated).
extern const float room_mirror_texels[];   // 128 x 128 x 3, octahedral
extern const float room_gloss_texels[];    // 32 x 32 x 3
extern const float room_satin_texels[];    // 16 x 16 x 3
extern const float room_diffuse_texels[];  // 16 x 16 x 3
extern const float room_sun_bake[];      // direction, light, the share of its disc seen

}  // namespace stones
