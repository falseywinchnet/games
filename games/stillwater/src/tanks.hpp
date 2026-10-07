#pragma once
// Stillwater's tanks: the planted tank (the original Riverscape), a coral reef and
// a river pool. Each is an archive in assets/scene (scene_src/build_scene.py and
// scene_src/build_tanks.py) and a TankStyle: its water, light, caustics and fish.
#include "riverscape_look.hpp"

#include <memory>
#include <string_view>

namespace sw {

enum class Tank : std::uint8_t { planted, reef, pool };

[[nodiscard]] const TankStyle& tank_style(Tank tank);
// The tank an archive belongs to, from its file name (riverscape, reef, pool).
[[nodiscard]] Tank tank_for_archive(std::string_view path);
[[nodiscard]] std::unique_ptr<ambient::Look> make_tank_look(Tank tank, const ambient::SceneData& scene);
// The props a tank's archive is dressed with as it loads (the chest), if any.
void dress_tank(Tank tank, ambient::SceneData& scene);

} // namespace sw
