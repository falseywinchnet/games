#pragma once
// The suite's shared renderer (shared/render): the 2D canvas, the 3D renderer and the
// mesh library, named in this game's namespace.
#include "r2d_canvas.hpp"
#include "r3d_mesh.hpp"
#include "r3d_renderer.hpp"

namespace ld {
using render::r2d::alpha;
using render::r2d::Canvas;
using render::r2d::Col;
using render::r2d::hex;
using render::r2d::Mask;
using render::r2d::Mat;
using render::r2d::mix;
using render::r2d::Paint;
using render::r2d::rgb;
using render::r2d::shade;
using render::r2d::Stop;
using render::r3d::box_mesh;
using render::r3d::cone_mesh;
using render::r3d::cross;
using render::r3d::cylinder_mesh;
using render::r3d::disc_mesh;
using render::r3d::dot;
using render::r3d::draw_mesh;
using render::r3d::draw_outline;
using render::r3d::hemisphere_mesh;
using render::r3d::len;
using render::r3d::Lighting;
using render::r3d::M34;
using render::r3d::Material;
using enum render::r3d::Material;
using render::r3d::Mesh;
using render::r3d::norm;
using render::r3d::rock_mesh;
using render::r3d::sphere_mesh;
using render::r3d::star_mesh;
using render::r3d::Tex;
using render::r3d::tint;
using render::r3d::torus_mesh;
using render::r3d::V3;
using render::r3d::Vtx;
using R3D = render::r3d::Renderer;
}  // namespace ld
