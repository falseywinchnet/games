#pragma once
// Every surface is painted at startup with the vector rasterizer, then copied
// into power-of-two texel maps: red brick, hotel carpet, acoustic ceiling
// tiles, doors and pads in five colours, the portal carpet that is *almost*
// the normal one, the elevator plate, the reward room, vaporwave posters and
// the retro computing signs. The snail's repaints are in paints.hpp.
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
// Where the painted posters and art are (poster_00.png, art_00.png ...); call before the first is drawn.
void use_poster_art(const std::string& dir);
int poster_count();
const Tex32& tex_poster(int i);                 // box art, flyers and ads from inside the maze's world
int art_count();                                // 0 without the painted files
const Tex32& tex_art(int i);                    // framed pictures: 3D renders, fractals, airbrush, screensavers ...
const char* pixel_glyph(char ch);               // the sign font: 35 '0'/'1' cells, 5 wide and 7 down
int carpet_themes();
const char* carpet_name(int theme);

}  // namespace mz
