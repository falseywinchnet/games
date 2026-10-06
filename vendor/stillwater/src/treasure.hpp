#pragma once
// A little sunken treasure chest for the tank: planked wood with iron bands, half
// sunk in the sand, its barrel lid ajar over a heap of gold, a few coins spilled in
// front, and now and then a bubble escaping from under the lid.
//
// It is ordinary fixed scenery added to the scene when it loads, so it is shaded once
// per size with shadows and material maps and costs nothing per frame; the gold takes
// the rippling light, which is where its glint comes from.
#include "archive.hpp"

namespace sw {

// Riverscape's sand height (the authoring script's ground_height).
[[nodiscard]] float ground_height(float x, float z);
// Appends the chest's meshes, placements and bubbles to a loaded scene.
void add_treasure(ambient::SceneData& scene);

} // namespace sw
