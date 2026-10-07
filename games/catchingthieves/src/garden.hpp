#pragma once
// The garden: a lawn, beds of tilled soil, clipped hedges, raccoon burrows and
// ribbed pumpkins, in the colours of the season, with the weather drifting
// through (petals, butterflies, falling leaves, snow, fireflies at night).
// Draws the scene from a three-quarter view above and answers picking.
#include "critters.hpp"
#include "ground.hpp"
#include "level.hpp"
#include "platform/r3d.hpp"

#include <memory>
#include <vector>

namespace ct {

struct FieldArt;    // field.hpp
struct HedgeCache;  // hedge.hpp

enum class Season { spring, summer, autumn, winter, night };
Season season_for(const std::string& section);

struct PumpkinView {
    V3 pos{};          // world, base on the ground
    double roll_x = 0, roll_y = 0;  // accumulated roll (radians) about the world x and y axes
    double wobble = 0; // a raccoon underneath shoves it about
    double hop = 0;    // a little jump (0..1 decaying)
    bool on_burrow = false;
    double glow = 0;   // the moment it lands on a burrow
    int seed = 0;      // per-pumpkin variety
};

struct Puff {          // dust kicked up by a push, a landing, a pop
    V3 pos;
    double age = 0, life = .6, size = .3;
    Col col{1, 1, 1, 1};
};

struct GardenState {
    Season season = Season::spring;
    std::vector<PumpkinView> pumpkins;
    BearPose bear;
    std::vector<CoonPose> coons;           // one per burrow (rise 0 when hidden or trapped)
    std::vector<int> trapped;              // per burrow: 1 when a pumpkin sits on it
    std::vector<double> paw_wiggle;        // per burrow: the trapped paw's wiggle phase
    std::vector<Puff> puffs;
    int hover_cell = -1;
    std::vector<int> path;                 // cells the bear will walk (click-to-move preview)
    int hint_from = -1, hint_dir = -1;     // a suggested push: the pumpkin's cell and direction
    double hint_t = 0;
    int stuck_cell = -1;                   // a pumpkin that can never be saved (it gets a sad little mark)
    double fade = 0;                       // 0..1 a wipe to the next garden
    double celebrate = 0;                  // 0..1 the garden is cleared
};

class Garden {
public:
    R3D r;
    void resize(int w, int h, int hud_w);  // hud_w: game pixels reserved on the right
    void set_level(const Level& lv);
    void render(const GardenState& s, double t);

    V3 cell_pos(int cell) const;           // world centre of a cell, on the ground
    V3 cell_pos(double x, double y) const;
    int pick_cell(double sx, double sy) const;
    void to_screen(V3 p, double& sx, double& sy) const;
    const Level& level() const { return lv_; }
    // the field around the garden, once it has grown (the plain lawn until then)
    void set_field(std::shared_ptr<const FieldArt> field) { field_ = std::move(field); }

private:
    Level lv_;
    int hud_w_ = 0;
    std::vector<int> deco_;  // per outside cell next to the garden: a decoration kind (0 none)
    std::shared_ptr<const FieldArt> field_;
    std::shared_ptr<HedgeCache> hedge_;  // the hedge's shell and shade, made once per garden (hedge.cpp)
    bool draw_field(const GardenState& s);  // field.cpp; false until the field for this season has grown
    Ground ground_;          // the soil of the beds and the burrows, painted once per season and size
    void fit_camera();
    void draw_ground(const GardenState& s, double t);
    void draw_hedges(const GardenState& s, double t);
    void draw_burrows(const GardenState& s, double t);
    void draw_pumpkins(const GardenState& s, double t);
    void draw_weather(const GardenState& s, double t);
    void draw_marks(const GardenState& s, double t);
    void sprite(V3 c, double size, const Tex* tex, Col col, std::uint16_t mat);
};

}  // namespace ct
