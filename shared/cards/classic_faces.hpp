#pragma once
// The classic card faces, drawn in code: plain white cards with crisp corner indices,
// the standard pip layouts (the lower pips upside down, as on a real card), aces with
// one large pip, and double-ended court cards whose figures (crowned kings with sword
// and beard, queens with a flower, jacks in a feathered cap) are mirrored about the
// middle. Every face is drawn once with the window's own painter into an offscreen
// frame, at the painted faces' size and corner, so the table lays the same finish
// (dimples, rolled edge, lamp, paper) over it as over every other card.
#include "gui_forms/basic_controls.hpp"

#include <array>

namespace games {
namespace gf = gui_forms;

// Fills `faces` (suit * 13 + rank - 1; suits clubs, diamonds, hearts, spades) with
// images registered with `window`. False if the window could not give a frame.
bool make_classic_faces(gf::Window& window, std::array<gf::ImageId, 52>& faces);

}  // namespace games
