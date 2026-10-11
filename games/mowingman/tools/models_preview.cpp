// Close views of the solid models, on a plain lawn, to judge them by eye.
//
//   mm_models_preview out.png what [ppm [heading [time]]]
//     what  mower0 mower1 mower2 (the liveries), gnome, granny, fountain, lounger, trunk, all
#include "art.hpp"
#include "png_writer.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

using Clock = std::chrono::steady_clock;

void lawn(mm::Canvas& canvas) {
    canvas.clear(mm::rgb(70, 112, 44));
    std::uint64_t random = 7;
    for (int k = 0; k < canvas.w * canvas.h / 40; ++k) {
        const double x = mm::random_range(random, 0, canvas.w);
        const double y = mm::random_range(random, 0, canvas.h);
        canvas.fill_rect(x, y, 1, 2, mm::rgb(90, 140, 56, 0.6f));
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s out.png what [ppm [heading [time]]]\n", argv[0]);
        return 2;
    }
    const std::string what = argv[2];
    const double ppm = argc > 3 ? std::atof(argv[3]) : 300;
    const double heading = argc > 4 ? std::atof(argv[4]) : -0.6;
    const double time = argc > 5 ? std::atof(argv[5]) : 1.3;
    const bool wide = what == "sheds" || what == "yard";
    const int width = static_cast<int>(ppm * (wide ? 9.0 : 3.2));
    const int height = static_cast<int>(ppm * (wide ? 8.0 : 2.6));
    mm::Canvas canvas;
    canvas.resize(width, height);
    lawn(canvas);
    mm::Frame frame{};
    frame.ppm = ppm;
    frame.ox = width * 0.5;
    frame.oy = height * 0.62;
    Clock::time_point start = Clock::now();
    double first_ms = 0;
    int frames = 0;
    for (; frames < 6; ++frames) {
        if (frames == 1) {
            first_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            start = Clock::now();
        }
        if (frames > 0)
            lawn(canvas);
        if (what == "fleet") {
            // every livery, from four sides
            for (int livery = 0; livery < 3; ++livery)
                for (int turn = 0; turn < 4; ++turn) {
                    mm::MowerPose pose{-1.2 + turn * 0.8 - 0.4, -1.1 + livery * 0.95, -2.4 + turn * 1.57, 0.5, 0.5, false};
                    mm::draw_mower(canvas, frame, pose, static_cast<mm::Livery>(livery), time);
                }
        } else if (what.rfind("mower", 0) == 0) {
            const int livery = what.size() > 5 ? what[5] - '0' : 0;
            mm::MowerPose pose{0, 0, heading, 0.5, 0.4, true};
            mm::draw_mower(canvas, frame, pose, static_cast<mm::Livery>(livery), time);
        } else if (what == "gnome") {
            mm::Gnome gnome{};
            gnome.state = mm::GnomeState::looking;
            gnome.height = 1;
            gnome.facing = heading;
            gnome.look = 0.1;
            gnome.dance = -1;
            mm::draw_gnome(canvas, frame, gnome, time);
        } else if (what == "dances") {
            // every move, a moment into it, in a row
            for (int move = 0; move < 6; ++move) {
                mm::Gnome gnome{};
                gnome.state = mm::GnomeState::looking;
                gnome.height = 1;
                gnome.x = -1.25 + (move % 3) * 1.25;
                gnome.y = move < 3 ? -0.9 : 0.55;
                gnome.facing = heading;
                gnome.dance = move;
                gnome.beat = time;
                mm::draw_gnome(canvas, frame, gnome, time);
            }
        } else if (what == "granny" || what == "granny_startled" || what == "granny_fleeing") {
            mm::Granny granny{};
            granny.state = what == "granny" ? mm::GrannyState::walking : what == "granny_startled" ? mm::GrannyState::startled : mm::GrannyState::fleeing;
            granny.facing = heading;
            granny.stride = time;
            mm::draw_granny(canvas, frame, granny);
        } else if (what == "grannies") {
            // marching, glaring, startled, running
            const mm::GrannyState states[4] = {mm::GrannyState::walking, mm::GrannyState::turning, mm::GrannyState::startled, mm::GrannyState::fleeing};
            for (int k = 0; k < 4; ++k) {
                mm::Granny granny{};
                granny.state = states[k];
                granny.x = -1.2 + (k % 2) * 1.6;
                granny.y = k < 2 ? -1.0 : 0.6;
                granny.facing = heading;
                granny.stride = time;
                granny.clock = 0.2;
                mm::draw_granny(canvas, frame, granny);
            }
        } else if (what == "yard") {
            // every kind of the things left on the lawn: grills, sandboxes, chairs
            struct Item {
                mm::PropKind kind;
                std::uint64_t style;
                double rx, ry, x, y;
                bool occupied;
            };
            const Item items[7] = {{mm::PropKind::grill, 0, 0.42, 0.34, -3.2, -2.4, false},
                                   {mm::PropKind::grill, 2, 0.8, 0.34, -0.6, -2.4, false},
                                   {mm::PropKind::chair, 1, 0.36, 0.82, 2.2, -2.4, false},
                                   {mm::PropKind::sandbox, 0, 0.8, 0.8, -2.8, 1.2, false},
                                   {mm::PropKind::sandbox, 1, 1.12, 0.62, 0.4, 1.4, false},
                                   {mm::PropKind::chair, 2, 0.36, 0.82, 3.0, 1.2, true},
                                   {mm::PropKind::chair, 0, 0.36, 0.82, 3.6, -2.4, false}};
            for (const Item& item : items) {
                mm::Prop prop{};
                prop.kind = item.kind;
                prop.rx = item.rx;
                prop.ry = item.ry;
                prop.x = item.x;
                prop.y = item.y;
                prop.occupied = item.occupied;
                prop.seed = (item.style << 33U) | (17U + static_cast<std::uint64_t>(item.x * 10 + 50));
                mm::draw_prop(canvas, frame, prop);
            }
        } else if (what == "fallen") {
            // the two grills standing, then knocked over with their coals alight
            std::uint64_t random = 5;
            std::vector<mm::Particle> fire{};
            for (int k = 0; k < 4; ++k) {
                mm::Prop prop{};
                prop.kind = mm::PropKind::grill;
                prop.rx = k % 2 == 0 ? 0.42 : 0.8;
                prop.ry = 0.34;
                prop.x = -1.6 + (k % 2) * 2.4;
                prop.y = k < 2 ? -0.9 : 0.9;
                prop.seed = (static_cast<std::uint64_t>(k % 2 == 0 ? 0 : 2) << 33U) | 23U;
                prop.toppled = k >= 2;
                prop.fall = 0.5;
                mm::draw_prop(canvas, frame, prop);
                if (!prop.toppled)
                    continue;
                for (int f = 0; f < 40; ++f) {
                    mm::Particle p{};
                    const bool flame = f < 26;
                    p.kind = flame ? mm::ParticleKind::flame : mm::ParticleKind::smoke;
                    p.x = prop.x + std::cos(prop.fall) * 0.65 + mm::random_range(random, -0.12, 0.12);
                    p.y = prop.y + std::sin(prop.fall) * 0.65 + mm::random_range(random, -0.08, 0.08);
                    p.life = flame ? 0.5 : 2.0;
                    p.age = mm::random_range(random, 0, p.life * 0.8);
                    p.z = 0.05 + p.age * (flame ? 0.75 : 0.35);
                    p.size = flame ? mm::random_range(random, 0.03, 0.06) : 0.07 + p.age * 0.05;
                    const double t = mm::random_range(random, 0, 1);
                    p.tint = flame ? (t < 0.4 ? 0xFFD24A : (t < 0.8 ? 0xFF8A1E : 0xE8461A)) : 0x7A7672;
                    fire.push_back(p);
                }
            }
            mm::draw_particles(canvas, frame, fire);
        } else if (what == "sheds") {
            for (int k = 0; k < 6; ++k) {
                mm::Prop prop{};
                prop.kind = mm::PropKind::shed;
                prop.rx = 0.9 + 0.1 * k;
                prop.ry = 0.75 + 0.04 * k;
                prop.x = -3.0 + (k % 3) * 3.0;
                prop.y = k < 3 ? -2.6 : 2.0;
                prop.seed = 101 + static_cast<std::uint64_t>(k) * 7919;
                mm::draw_prop(canvas, frame, prop);
            }
        } else if (what == "fountain" || what == "lounger" || what == "birdbath") {
            mm::Prop prop{};
            prop.kind = what == "fountain" ? mm::PropKind::fountain : what == "birdbath" ? mm::PropKind::birdbath : mm::PropKind::chair;
            prop.rx = what == "fountain" ? 0.62 : what == "birdbath" ? 0.38 : 0.36;
            prop.ry = what == "fountain" ? 0.62 : what == "birdbath" ? 0.38 : 0.82;
            prop.angle = heading;
            prop.occupied = true;
            prop.seed = 11;
            mm::draw_prop(canvas, frame, prop);
            mm::draw_prop_live(canvas, frame, prop, time);
        } else if (what == "trunk") {
            mm::Tree tree{};
            tree.trunk = 0.12;
            tree.crown = 1.2;
            tree.ring = 1.4;
            tree.seed = 5;
            mm::draw_tree_foot(canvas, frame, tree);
        }
    }
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count() / (frames - 1);
    static_cast<void>(kit::write_png(argv[1], width, height, canvas.px));
    std::printf("%s at %.0f ppm: first %.1f ms (making the model), then %.2f ms a frame (lawn fill included)\n", what.c_str(), ppm, first_ms, ms);
    return 0;
}
