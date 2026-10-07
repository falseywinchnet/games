#pragma once
// Every surface is painted at startup with the vector rasterizer, then copied
// into power-of-two texel maps: red brick, hotel carpet, acoustic ceiling
// tiles, doors and pads in five colours, the portal carpet that is *almost*
// the normal one, the elevator plate, the reward room, vaporwave posters and
// the retro computing signs, and the snail's bizarre repaints.
#include "soft3d.hpp"

#include <string>

namespace mz {

constexpr int kColors = 5;  // red, blue, green, yellow, violet
std::uint32_t key_rgb(int color);

enum class Wall : std::uint8_t { brick, gold, door, poster };
const Tex32& tex_brick();
const Tex32& tex_brick_alt(int variant);       // a few weathered variants so long runs don't tile
const Tex32& tex_carpet(int theme);            // theme: the level's carpet design
const Tex32& tex_portal_carpet(int theme);     // the same carpet, very slightly wrong
const Tex32& tex_ceiling(int theme);
const Tex32& tex_door(int color);
const Tex32& tex_pad(int color, bool lit);
const Tex32& tex_elevator();
const Tex32& tex_gold_wall();
const Tex32& tex_marble();
const Tex32& tex_glow();
int poster_count();
const Tex32& tex_poster(int i);                 // vaporwave and retro-computing wall signs
int paint_count();
const Tex32& tex_paint(int i);                  // the snail's repaints (some of them glitch)
bool paint_glitches(int i);
int carpet_themes();
const char* carpet_name(int theme);

}  // namespace mz
