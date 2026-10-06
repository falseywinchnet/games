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
    const bool wide = what == "sheds";
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
