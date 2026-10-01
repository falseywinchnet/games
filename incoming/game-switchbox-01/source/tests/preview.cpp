// Headless frame renders for look development: writes PPM images.
//   preview faces out.ppm            every expression on one sheet
//   preview pose out.ppm W H         the stage with a fixed test pose
#include "actor.hpp"
#include "face.hpp"
#include "stage.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace sbx;

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
    const Eyes eyes[] = {Eyes::open, Eyes::half, Eyes::happy, Eyes::squeeze, Eyes::wide, Eyes::angry, Eyes::blink, Eyes::sparkle, Eyes::teary, Eyes::swirl};
    const Mouth mouths[] = {Mouth::smile, Mouth::cat, Mouth::open_smile, Mouth::grin_tongue, Mouth::pout, Mouth::frown, Mouth::wavy, Mouth::shout, Mouth::o_small, Mouth::bleh};
    const Brow brows[] = {Brow::neutral, Brow::raised, Brow::angry, Brow::worried, Brow::neutral, Brow::angry, Brow::neutral, Brow::raised, Brow::worried, Brow::worried};
    Canvas c;
    c.resize(256 * 5, 256 * 2);
    for (int i = 0; i < 10; ++i) {
        Face f;
        f.eyes = eyes[i]; f.mouth = mouths[i]; f.brow = brows[i]; f.blush = i % 3; f.look_x = (i % 5) - 2;
        const Tex& t = face_texture(f);
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
        const int W = argc > 3 ? std::atoi(argv[3]) : 590, H = argc > 4 ? std::atoi(argv[4]) : 400;
        const double up = argc > 5 ? std::atof(argv[5]) : 1.0;
        Stage st;
        st.resize(W, H);
        StageState s;
        s.lid = up;
        s.girl.root = {0, .5, -.2 + 1.0 * up};
        s.girl.face.eyes = Eyes::open;
        s.girl.face.mouth = Mouth::open_smile;
        s.sw[2].on = 1; s.sw[0].lamp = 1; s.sw[4].lamp = 1;
        s.sw[2].on = 1;
        s.girl.hand[1] = Stage::knob(2, 1);
        s.girl.reach[1] = argc > 6 ? std::atof(argv[6]) : 0;
        if (argc > 7) {  // close-up on the head
            st.r.scale *= std::atof(argv[7]);
            st.r.target = head_center(s.girl);
            st.r.ay = .5;
            st.r.set_camera();
        }
        if (argc > 8) s.girl.head_yaw = std::atof(argv[8]);
        s.girl.head_pitch = argc > 9 ? std::atof(argv[9]) : -.25;
        st.render(s, 0);
        Canvas c;
        c.resize(W, H);
        st.r.present(c, 1, 0, 0, true);
        write_ppm(argv[2], c);
        std::printf("tris %lld\n", st.r.tris_drawn);
        return 0;
    }
    if (argc >= 3 && !std::strcmp(argv[1], "anim")) {
        // a scripted session rendered into a contact sheet
        //   preview anim out.ppm DUR EVERY T0 "t:code,..."   codes: n next correct, w a wrong down switch, h head, 0-5 a switch
        const int W = 360, H = 244, cols = 6;
        const double fps = 30, dur = argc > 3 ? std::atof(argv[3]) : 12, every = argc > 4 ? std::atof(argv[4]) : .25;
        const double t0 = argc > 5 ? std::atof(argv[5]) : 0;
        const std::string script = argc > 6 ? argv[6] : "0.4:n,0.6:n,1.5:w,4.5:n,4.7:n,4.9:n,5.1:n,5.3:n,5.5:n";
        Stage st;
        st.resize(W, H);
        if (const char* z = std::getenv("PREVIEW_ZOOM")) {  // close-up on the switch row
            st.r.scale *= std::atof(z);
            st.r.target = {std::getenv("PREVIEW_X") ? std::atof(std::getenv("PREVIEW_X")) : 0.0, -.1, 1.9};
            st.r.ay = .55;
            st.r.set_camera();
        }
        StageState s;
        Actor a(3);
        Puzzle pz(5);
        struct Ev { double t; std::string code; };
        std::vector<Ev> evs;
        for (size_t pos = 0; pos < script.size();) {
            size_t e = script.find(',', pos);
            if (e == std::string::npos) e = script.size();
            const std::string item = script.substr(pos, e - pos);
            const size_t c = item.find(':');
            evs.push_back({std::atof(item.c_str()), item.substr(c + 1)});
            pos = e + 1;
        }
        const int frames = static_cast<int>((dur - t0) / every + 1e-6);
        const int rows = (frames + cols - 1) / cols;
        Canvas sheet;
        sheet.resize(W * cols, H * rows);
        Canvas c;
        c.resize(W, H);
        size_t ei = 0;
        int shot = 0;
        int hold = -1;
        bool hold_blocked = false, steal = false;
        double next_shot = t0;
        for (int f = 0; f <= static_cast<int>(dur * fps); ++f) {
            const double t = f / fps;
            while (ei < evs.size() && evs[ei].t <= t) {
                const std::string& code = evs[ei].code;
                int sw = -1;
                if (code == "h") a.head_clicked();
                else if (code[0] == 'H') { hold = std::atoi(code.c_str() + 1); sw = hold; }   // press and hold
                else if (code == "R") hold = -1;                                               // let go
                else if (code[0] == 'S') { sw = std::atoi(code.c_str() + 1); steal = true; }   // a fourth repeat: theft
                else if (code[0] == 'c') a.hole_clicked(std::atoi(code.c_str() + 1));
                else if (code == "n") sw = pz.combination()[static_cast<size_t>(std::min(pz.progress(), kSwitches - 1))];
                else if (code == "w") {
                    for (int k = 1; k < kSwitches; ++k) {
                        const int cand = (pz.first() + k) % kSwitches;
                        if (s.sw[static_cast<size_t>(cand)].on <= 0 && s.sw[static_cast<size_t>(cand)].sink <= 0 && cand != pz.combination()[static_cast<size_t>(pz.progress())]) { sw = cand; break; }
                    }
                } else sw = std::atoi(code.c_str());
                if (sw >= 0 && s.sw[static_cast<size_t>(sw)].on <= 0 && s.sw[static_cast<size_t>(sw)].sink <= .05) {
                    FlipResult r = pz.flip(sw);
                    if (r.accepted) {
                        s.sw[static_cast<size_t>(sw)].on = 1;
                        a.flipped(sw, r.correct, r.solved);
                        if (steal) a.steal(sw);
                        if (r.reaction != Reaction::none) a.react(r.reaction, pz.first());
                        std::printf("t=%.2f flip %d %s reaction %d\n", t, sw, r.correct ? "right" : "wrong", static_cast<int>(r.reaction));
                    }
                }
                steal = false;
                ++ei;
            }
            a.set_hold(hold_blocked ? -1 : hold);
            a.update(1 / fps, s);
            if (a.take_forced_release()) hold_blocked = true;
            V3 drop;
            if (a.take_pointer_drop(drop)) hold_blocked = true;
            s.cursor = a.carrying_pointer() || (hold >= 0 && !hold_blocked && a.resting_on_hold());
            if (a.carrying_pointer()) s.cursor_at = a.pointer_at();
            else if (s.cursor) s.cursor_at = Stage::knob(hold, s.sw[static_cast<size_t>(hold)].on) + V3{0, -.06, .09};
            for (int i = 0; i < kSwitches; ++i) s.sw[static_cast<size_t>(i)].lamp += ((pz.lamp(i) ? 1.0 : 0.0) - s.sw[static_cast<size_t>(i)].lamp) * .3;
            if (a.take_reset_request()) pz.new_combination();
            a.cues.clear();
            if (t + 1e-9 >= next_shot && shot < frames) {
                st.render(s, t);
                st.r.present(c, 1, 0, 0, true);
                for (int y = 0; y < H; ++y)
                    std::memcpy(&sheet.px[static_cast<size_t>(((shot / cols) * H + y) * sheet.w + (shot % cols) * W) * 4], &c.px[static_cast<size_t>(y * W) * 4], static_cast<size_t>(W) * 4);
                ++shot;
                next_shot += every;
            }
        }
        write_ppm(argv[2], sheet);
        return 0;
    }
    std::fprintf(stderr, "usage: preview faces|pose out.ppm [W H up reach]\n");
    return 2;
}
