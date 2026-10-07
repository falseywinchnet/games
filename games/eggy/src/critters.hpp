#pragma once
#include "r3d.hpp"

namespace eggy {

struct Textures;

// positions in render space (z already scaled); t = real seconds
void draw_deer(R3D& r, double x, double y, double z, double heading, double t, int variant);
void draw_cat(R3D& r, double x, double y, double z, double heading, double t, int variant);
void draw_frog(R3D& r, double x, double y, double z, double heading, double t, int variant);
void draw_fish(R3D& r, double x, double y, double z, double angle, double tilt, int variant);
void draw_campfire(R3D& r, const Textures& T, double x, double y, double z, double t);
void draw_camp_tent(R3D& r, const Textures& T, double x, double y, double z, double facing, double size, Col tint);

}  // namespace eggy
