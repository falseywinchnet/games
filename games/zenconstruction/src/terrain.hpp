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
// How far the brook's course has swung north (+) or south (-) out beyond the worksite, so
// that it winds away out of sight: none within 5 m of the work, where the ground is played on.
double brook_bend(double x);
double brook_near_at(double x);
double brook_far_at(double x);
double terrain_height(Terrain kind, double x, double y);
// The ground's height anywhere: the bank, the brook's bed or the far bank.
double ground_height(double x, double y);
// How much the ground at (x, y) is the gravel bar the work stands on (1) rather than the
// meadow round it (0): the bar, and a strip of shingle along the water's near edge.
double gravel_bar(double x, double y);
phys::Vec3 ledge_rim(int index);
phys::Vec3 ledge_foot(int index);
std::shared_ptr<const phys::GroundSurface> worksite_ground();
} // namespace zc
