// Headless frame renders for look development: writes PPM images.
//   preview faces out.ppm
#include "lair.hpp"
#include "vactor.hpp"
#include "vface.hpp"

#include "platform/raster.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace fp;

static void write_ppm(const char* path, const Canvas& c) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6 %d %d 255\n", c.w, c.h);
    for (int i = 0; i < c.w * c.h; ++i) {
        const unsigned char q[3] = {c.px[i * 4 + 2], c.px[i * 4 + 1], c.px[i * 4]};
        std::fwrite(q, 1, 3, f);
    }
    std::fclose(f);
}

static void faces(const char* path) {
    const VEyes eyes[] = {VEyes::menace, VEyes::half, VEyes::closed, VEyes::wide, VEyes::furious, VEyes::squint, VEyes::glint, VEyes::laugh, VEyes::twitch, VEyes::menace};
    const VMouth mouths[] = {VMouth::smirk, VMouth::flat, VMouth::purse, VMouth::gasp, VMouth::shout, VMouth::sneer, VMouth::grin, VMouth::laugh, VMouth::grimace, VMouth::frown};
    const VBrow brows[] = {VBrow::flat, VBrow::flat, VBrow::arched, VBrow::worried, VBrow::furious, VBrow::quizzical, VBrow::arched, VBrow::arched, VBrow::worried, VBrow::furious};
    Canvas c;
    c.resize(256 * 5, 256 * 2);
    for (int i = 0; i < 10; ++i) {
        VFace f;
        f.eyes = eyes[i]; f.mouth = mouths[i]; f.brow = brows[i]; f.look_x = (i % 5) - 2;
        const Tex& t = vface_texture(f);
        for (int y = 0; y < t.h; ++y)
            for (int x = 0; x < t.w; ++x) {
                const std::uint32_t p = t.px[static_cast<size_t>(y * t.w + x)];
                std::uint8_t* o = &c.px[static_cast<size_t>(((i / 5) * 256 + y) * c.w + (i % 5) * 256 + x) * 4];
                o[0] = p & 255; o[1] = (p >> 8) & 255; o[2] = (p >> 16) & 255; o[3] = 255;
            }
    }
    write_ppm(path, c);
}

int main(int argc, char** argv) {
    if (argc >= 3 && !std::strcmp(argv[1], "faces")) { faces(argv[2]); return 0; }
    if (argc >= 3 && !std::strcmp(argv[1], "pose")) {
        // preview pose out.ppm [W H steeple zoom]
        const int W = argc > 3 ? std::atoi(argv[3]) : 560, H = argc > 4 ? std::atoi(argv[4]) : 380;
        const bool steeple = argc > 5 && std::atoi(argv[5]);
        Lair lair;
        lair.resize(W, H);
        if (argc > 6) {
            LairState tmp;
            lair.r.scale *= std::atof(argv[6]);
            lair.r.target = villain_head_center(tmp.villain) + V3{0, 0, -.3};
            lair.r.ay = .45;
            lair.r.set_camera();
        }
        LairState s;
        s.console.draft = {0, 3, -1, 5};
        s.console.can_check = false;
        s.console.turn = 4;
        s.console.last = {1, 2};
        s.console.pins = 1;
        if (steeple) {
            const V3 hc = villain_head_center(s.villain);
            for (int k = 0; k < 2; ++k) {
                s.villain.hand[static_cast<size_t>(k)] = hc + V3{k ? .22 : -.22, -.8, -1.05};
                s.villain.reach[static_cast<size_t>(k)] = 1;
                s.villain.palm[static_cast<size_t>(k)] = norm(V3{k ? -.5 : .5, -.2, 1});
            }
            s.villain.face.eyes = VEyes::half;
            s.villain.face.mouth = VMouth::smirk;
        }
        lair.render(s, 0);
        Canvas c;
        c.resize(W, H);
        lair.r.present(c, 1, 0, 0, true);
        write_ppm(argv[2], c);
        std::printf("tris %lld\n", lair.r.tris_drawn);
        return 0;
    }
    if (argc >= 3 && !std::strcmp(argv[1], "anim")) {
        // preview anim out.ppm DUR EVERY T0 "t:code,..."  codes: n new game, w a wrong guess, c a close guess (3 exact), s solve, l lose, p poke
        const int W = 360, H = 244, cols = 6;
        const double fps = 30, dur = argc > 3 ? std::atof(argv[3]) : 12, every = argc > 4 ? std::atof(argv[4]) : .3;
        const double t0 = argc > 5 ? std::atof(argv[5]) : 0;
        const std::string script = argc > 6 ? argv[6] : "0:n,9:w,14:c,19:s";
        Lair lair;
        lair.resize(W, H);
        if (const char* z = std::getenv("PREVIEW_ZOOM")) {  // close-up on him
            LairState tmp;
            lair.r.scale *= std::atof(z);
            lair.r.target = villain_head_center(tmp.villain) + V3{0, 0, -.55};
            lair.r.set_camera();
        }
        LairState s;
        VillainActor a(4);
        Board board(9);
        struct Ev { double t; char code; };
        std::vector<Ev> evs;
        for (size_t pos = 0; pos < script.size();) {
            size_t e = script.find(',', pos);
            if (e == std::string::npos) e = script.size();
            const std::string item = script.substr(pos, e - pos);
            evs.push_back({std::atof(item.c_str()), item[item.find(':') + 1]});
            pos = e + 1;
        }
        const int frames = static_cast<int>((dur - t0) / every + 1e-6);
        Canvas sheet;
        sheet.resize(W * cols, H * ((frames + cols - 1) / cols));
        Canvas c;
        c.resize(W, H);
        size_t ei = 0;
        int shot = 0;
        double next_shot = t0, speech_t = 0, pins_t = -1;
        for (int f = 0; f <= static_cast<int>(dur * fps); ++f) {
            const double t = f / fps;
            while (ei < evs.size() && evs[ei].t <= t) {
                const char code = evs[ei].code;
                if (code == 'n') { board.new_game(); s.console = ConsoleState{}; s.console.reboot = .01; a.new_game(); }
                else if (code == 'p') a.poked();
                else {
                    Code g = board.secret();
                    if (code == 'w') g = {(g[0] + 1) % 6, (g[1] + 1) % 6, (g[2] + 1) % 6, (g[3] + 1) % 6};
                    if (code == 'c') g[3] = (g[3] + 1) % 6;
                    if (code == 'l') { while (!board.over()) { for (int i = 0; i < 4; ++i) board.set(i, (board.secret()[static_cast<size_t>(i)] + 1) % 6); board.submit(); } }
                    else { for (int i = 0; i < 4; ++i) board.set(i, g[static_cast<size_t>(i)]); board.submit(); }
                    s.console.last = board.rows().back().score;
                    s.console.pins = 0;
                    s.console.turn = board.turns_used() + 1;
                    s.console.secret = board.secret();
                    a.submitted(s.console.last, board.turns_left(), board.won(), board.lost());
                }
                ++ei;
            }
            // the view's job, simplified: type speech, pop it, reveal pins
            if (!a.speech.empty()) {
                speech_t += 1 / fps;
                a.set_talking(speech_t < a.speech.front().text.size() / 30.0);
                if (speech_t > a.speech.front().text.size() / 30.0 + a.speech.front().hold) { a.speech.pop_front(); speech_t = 0; }
            } else a.set_talking(false);
            if (a.take_reveal_pins()) pins_t = 0;
            if (pins_t >= 0) { pins_t += 1 / fps; s.console.pins = std::min(1.0, pins_t / .6); }
            if (a.take_reveal_secret()) s.console.reveal = .01;
            if (s.console.reveal > 0) s.console.reveal = std::min(1.0, s.console.reveal + 1 / fps);
            if (s.console.reboot > 0) { s.console.reboot += 1 / fps / .8; if (s.console.reboot >= 1) s.console.reboot = 0; }
            s.console.alarm = board.turns_left() <= 3 ? 1 : 0;
            a.update(1 / fps, s);
            a.cues.clear();
            if (t + 1e-9 >= next_shot && shot < frames) {
                lair.render(s, t);
                lair.r.present(c, 1, 0, 0, true);
                for (int y = 0; y < H; ++y)
                    std::memcpy(&sheet.px[static_cast<size_t>(((shot / cols) * H + y) * sheet.w + (shot % cols) * W) * 4], &c.px[static_cast<size_t>(y * W) * 4], static_cast<size_t>(W) * 4);
                if (!a.speech.empty() && std::fmod(t, 1.0) < every) std::printf("%5.1f  %s\n", t, a.speech.front().text.c_str());
                ++shot;
                next_shot += every;
            }
        }
        write_ppm(argv[2], sheet);
        return 0;
    }
    std::fprintf(stderr, "usage: preview faces|pose|anim out.ppm\n");
    return 2;
}
