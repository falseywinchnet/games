#pragma once
#include "physics.hpp"
#include <cstdint>
#include <memory>

namespace zc {
enum class Terrain { bank, brook_bed, far_bank };
double smoothstep(double a, double b, double x);
std::uint32_t hash2(int x, int y, std::uint32_t seed);
double hash_unit(int x, int y, std::uint32_t seed);
double value_noise(double x, double y, std::uint32_t seed);
double ledge_radius(double angle);
bool on_ledge(double x, double y, double margin);
double brook_near_at(double x);
double brook_far_at(double x);
double terrain_height(Terrain kind, double x, double y);
phys::Vec3 ledge_rim(int index);
phys::Vec3 ledge_foot(int index);
std::shared_ptr<const phys::GroundSurface> worksite_ground();
} // namespace zc
