#pragma once
// The stones the rocks are made of: a small library of procedural surface
// textures (128 x 128, seamless) in the stone's own colours, from coarse
// crystalline granites to fine slate. A rock picks one at random from those
// its class is likely to be; its vertex colours then only shade, mottle,
// vein and lichen it.
#include "platform/r3d.hpp"

namespace zc {

enum class Stone { pink_granite, grey_granite, sandstone, slate, basalt, quartzite, limestone, greywacke };
constexpr int kStoneCount = 8;

const Tex& stone_texture(Stone stone);
// the texture's average colour (to turn absolute colours, lichen and veins,
// into tints over it)
Col stone_mean(Stone stone);
const char* stone_name(Stone stone);
// an orientation test pattern (arrows), for checking that rocks draw the way they lie
const Tex& arrow_texture();

}  // namespace zc
