// Headless frame renders for look development: writes PPM images.
//   preview play out.ppm [W H]     the box mid-game: probes answered, markers placed, a beam in flight
//   preview reveal out.ppm [W H]   the box opened: atoms, replayed beams, the reckoning
#include "chamber.hpp"

#include "platform/raster.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace ap;

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

static V3 wpt(Pt p) { return Chamber::cell(p.x, p.y, Chamber::kBeamZ); }

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: preview play|reveal out.ppm [W H]\n"); return 1; }
    const int W = argc > 3 ? std::atoi(argv[3]) : 560, H = argc > 4 ? std::atoi(argv[4]) : 380;
    const bool reveal = !std::strcmp(argv[1], "reveal");
    Chamber ch;
    ch.resize(W, H);
    ChamberState s;
    Box box(42);
    box.new_box();
    // fire a handful of ports
    const int fire[] = {2, 5, 9, 13, 18, 22, 27, 30};
    for (int p : fire) {
        bool fresh;
        const Probe& pr = box.fire(p, fresh);
        if (!fresh) continue;
        PortView& a = s.ports[static_cast<size_t>(pr.port)];
        a.kind = pr.kind == Outcome::hit ? 1 : pr.kind == Outcome::reflect ? 2 : 3;
        a.pair = pr.pair;
        if (pr.kind == Outcome::exit) s.ports[static_cast<size_t>(pr.exit)] = a;
    }
    s.points = box.points();
    int marked = 0;
    for (int c = 0; c < 64 && marked < 3; ++c)
        if (box.atom(c) || c == 20) { box.toggle_mark(c); ++marked; }
    box.toggle_empty(9);
    box.toggle_empty(10);
    s.marks = box.state().marks;
    s.empties = box.state().empties;
    s.markers_left = kAtoms - box.marks();
    s.atoms = box.atoms();
    const double t = argc > 5 ? std::atof(argv[5]) : 3.0;
    if (!reveal) {
        s.hover_port = 11;
        s.hover_cell = 35;
        s.glow = .6;
        s.ripple = .3;
        s.ripple_at = Chamber::hole(2);
        s.ports[3].charge = .7;
        Bolt b;
        b.pts = {Chamber::muzzle(2), {Chamber::hole(2).x, Chamber::hole(2).y, Chamber::kBeamZ}};
        b.col = hex(0x56F0FF);
        b.head = .5;
        s.bolts.push_back(b);
    } else {
        s.fog = 0;
        s.lid = 1;
        s.atoms_on = 1;
        s.judge = 1;
        s.lever = 1;
        for (const Probe& pr : box.probes()) {
            const Trace tr = trace(box.atoms(), pr.port);
            Bolt b;
            b.pts.push_back(Chamber::muzzle(pr.port));
            for (const Pt& p : tr.path) b.pts.push_back(wpt(p));
            if (tr.kind == Outcome::exit) b.pts.push_back(Chamber::muzzle(tr.exit));
            b.keep = true;
            b.col = pr.kind == Outcome::hit ? hex(0xFF3B5C) : pr.kind == Outcome::reflect ? hex(0xF4F7FF) : hex(0x4FC3FF);
            b.head = 1e9;
            s.bolts.push_back(b);
        }
    }
    ch.render(s, t);
    Canvas c;
    c.resize(W, H);
    ch.r.present(c, 1, 0, 0, true);
    write_ppm(argv[2], c);
    std::printf("tris %lld\n", ch.r.tris_drawn);
    return 0;
}
