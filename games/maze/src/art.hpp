#pragma once
// The maze's choosable surfaces: floors, walls and ceilings. Each list's order is permanent
// (saves keep the index). Floors and ceilings are tiles covering 4 x 4 squares; a wall
// tile covers `wall_span()` faces along a wall, so op art runs on down a corridor. Each
// surface is painted the first time it is wanted, and kept.
//
// "Calm" surfaces are the quiet ones a calmer level draws from.
#include "maze.hpp"
#include "soft3d.hpp"

#include <cstdint>

namespace mz {

constexpr int kFloorSpan = 4, kCeilingSpan = 4;  // squares a floor or ceiling tile covers

int floor_count();
const char* floor_name(int i);
bool floor_calm(int i);
const Tex32& tex_floor(int i);
const Tex32& tex_floor_portal(int i);  // the same, very slightly wrong

int wall_count();
const char* wall_name(int i);
bool wall_calm(int i);
int wall_span(int i);                      // faces one tile covers along a wall
const Tex32& tex_wall(int i, int variant);  // brick and stone come in a few weathered variants

int ceiling_count();
const char* ceiling_name(int i);
bool ceiling_calm(int i);
const Tex32& tex_ceiling_art(int i);

// Pictures on the walls: one or two posters and three or four pieces of art, on faces picked
// from `seed`; whatever the maze was generated with comes down first. A face's look is then
// 1 + a poster's index, or kArtLook + a piece's index.
constexpr int kArtLook = 128;
void hang_pictures(Level& lv, std::uint64_t seed, int posters, int pieces);

}  // namespace mz
