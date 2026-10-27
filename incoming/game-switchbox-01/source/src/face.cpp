#include "face.hpp"

#include "platform/raster.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace sbx {

namespace {
constexpr int kTex = 256;
const Col kSkin = hex(0xFCE4D6);
const Col kLash = hex(0x3B2219), kBrowCol = hex(0x6B402A), kLine = hex(0x7A3A30);
const Col kIrisTop = hex(0x5A3216), kIrisBot = hex(0xE0A445), kPupil = hex(0x2A170E);
const Col kWhiteEye = hex(0xFFFFFF), kWhiteShade = hex(0xE6E2F0);
const Col kMouthIn = hex(0x9B3442), kTongue = hex(0xF28C93), kBlush = hex(0xF59AA0);

// head-sphere coordinates (x right on screen, z up) to texture pixels
double X(double x) { return kTex * (.5 + x * kFaceK); }
double Y(double z) { return kTex * (.5 - (z - kFaceZ) * kFaceK); }
double S(double d) { return d * kTex * kFaceK; }  // a head-space length in pixels

constexpr double kEyeX = .37, kEyeZ = -.12, kEyeW = .175, kEyeH = .25;  // half sizes

void arc_stroke(Canvas& c, double x0, double y0, double cx, double cy, double x1, double y1, Col col, double w) {
    c.begin(); c.move(x0, y0); c.quad(cx, cy, x1, y1); c.stroke(col, w);
}

// One eye. `side` is -1 for the eye on the screen left, +1 for the right.
void eye(Canvas& c, const Face& f, int side) {
    const double cx = X(side * kEyeX), cy = Y(kEyeZ);
    const double w = S(kEyeW), h = S(kEyeH);
    const double lw = S(.045);  // lash weight
    const double outer = side * w;  // toward the side of the face
    switch (f.eyes) {
        case Eyes::happy:  // ^ ^
            arc_stroke(c, cx - w * .95, cy + h * .25, cx, cy - h * 1.1, cx + w * .95, cy + h * .25, kLash, lw * 1.1);
            return;
        case Eyes::blink:  // relaxed closed: a soft downward curve with a lash flick
            arc_stroke(c, cx - w, cy, cx, cy + h * .55, cx + w, cy, kLash, lw);
            arc_stroke(c, cx + outer * .9, cy + h * .05, cx + outer * 1.15, cy - h * .05, cx + outer * 1.3, cy - h * .25, kLash, lw * .6);
            return;
        case Eyes::squeeze: {  // > <, pointing toward the nose
            const double d = -side;
            c.begin();
            c.move(cx - d * w * .8, cy - h * .55);
            c.line(cx + d * w * .75, cy);
            c.line(cx - d * w * .8, cy + h * .55);
            c.stroke(kLash, lw * 1.05);
            return;
        }
        case Eyes::swirl: {
            c.begin();
            const int n = 40;
            for (int i = 0; i <= n; ++i) {
                const double a = i * .5 * side, r = w * .85 * i / n;
                const double px = cx + std::cos(a) * r, py = cy + std::sin(a) * r * 1.15;
                i == 0 ? c.move(px, py) : c.line(px, py);
            }
            c.stroke(kLash, lw * .55);
            return;
        }
        default: break;
    }
    const bool wide = f.eyes == Eyes::wide;
    const double ew = wide ? w * 1.05 : w, eh = wide ? h * 1.08 : h;
    // eye white with a lilac shadow under the upper lid
    c.begin(); c.ellipse(cx, cy, ew, eh); c.fill(Paint::lin(cx, cy - eh, cx, cy + eh * .2, {{0, kWhiteShade}, {.45f, kWhiteEye}, {1, kWhiteEye}}));
    // iris and pupil follow the gaze
    const double ix = cx + f.look_x * w * .16, iy = cy - f.look_y * h * .12 + h * .06;
    const double ir = wide ? w * .5 : w * .82, irh = wide ? h * .52 : h * .86;
    c.begin(); c.ellipse(ix, iy, ir, irh); c.fill(Paint::lin(ix, iy - irh, ix, iy + irh, {{0, kIrisTop}, {.45f, mix(kIrisTop, kIrisBot, .45f)}, {1, kIrisBot}}));
    c.begin(); c.ellipse(ix, iy, ir, irh); c.stroke(kLash, S(.012));
    c.fill_ellipse(ix, iy - irh * .12, ir * .48, irh * .5, kPupil);
    // a warm glow in the lower iris
    c.begin(); c.ellipse(ix, iy + irh * .45, ir * .62, irh * .3); c.fill(alpha(hex(0xFFD27A), .55f));
    if (f.eyes == Eyes::sparkle) {  // four-point stars for highlights
        auto star = [&](double sx, double sy, double r) {
            c.begin(); c.move(sx, sy - r); c.quad(sx, sy, sx + r, sy); c.quad(sx, sy, sx, sy + r); c.quad(sx, sy, sx - r, sy); c.quad(sx, sy, sx, sy - r); c.close();
            c.fill(kWhiteEye);
        };
        star(ix - ir * .35, iy - irh * .4, ir * .62);
        star(ix + ir * .4, iy + irh * .35, ir * .32);
    } else if (!wide) {
        c.fill_ellipse(ix - ir * .38, iy - irh * .42, ir * .36, irh * .27, kWhiteEye);
        c.fill_circle(ix + ir * .42, iy + irh * .38, ir * .16, kWhiteEye);
    } else {
        c.fill_circle(ix - ir * .3, iy - irh * .3, ir * .22, kWhiteEye);
    }
    if (f.eyes == Eyes::teary) {  // brimming: a glossy band along the lower lid and a tear at the corner
        c.begin(); c.ellipse(cx, cy + eh * .62, ew * .95, eh * .3); c.fill(alpha(hex(0xBFE6FF), .7f));
        c.begin(); c.move(cx + outer * .9, cy + eh * .7); c.quad(cx + outer * 1.25, cy + eh * 1.2, cx + outer * .95, cy + eh * 1.35);
        c.quad(cx + outer * .65, cy + eh * 1.15, cx + outer * .9, cy + eh * .7); c.fill(hex(0x9AD4FF));
    }
    // lids: half-closed and angry eyes cover the top of the eye with skin
    if (f.eyes == Eyes::half) {
        c.begin(); c.rect(cx - ew * 1.3, cy - eh * 1.4, ew * 2.6, eh * 1.25); c.fill(kSkin);
        arc_stroke(c, cx - ew * 1.05, cy - eh * .1, cx, cy - eh * .25, cx + ew * 1.05, cy - eh * .1, kLash, lw);
    } else if (f.eyes == Eyes::angry) {
        c.begin();
        c.move(cx - ew * 1.4, cy - eh * 1.5); c.line(cx + ew * 1.4, cy - eh * 1.5);
        c.line(cx + ew * 1.4, cy - eh * (side > 0 ? .1 : .55)); c.line(cx - ew * 1.4, cy - eh * (side > 0 ? .55 : .1)); c.close();
        c.fill(kSkin);
        c.stroke_line(cx - ew * 1.1, cy - eh * (side > 0 ? .5 : .12), cx + ew * 1.1, cy - eh * (side > 0 ? .12 : .5), kLash, lw * 1.1);
    } else {
        // upper lash line, thick, with a flick at the outer corner
        c.begin();
        c.move(cx - ew * 1.08, cy - eh * .35);
        c.quad(cx, cy - eh * 1.32, cx + ew * 1.08, cy - eh * .35);
        c.stroke(kLash, lw);
        arc_stroke(c, cx + outer * 1.0, cy - eh * .4, cx + outer * 1.25, cy - eh * .55, cx + outer * 1.32, cy - eh * .85, kLash, lw * .65);
        // lower lash: a short soft line
        arc_stroke(c, cx - ew * .5, cy + eh * .98, cx, cy + eh * 1.08, cx + ew * .5, cy + eh * .98, alpha(kLash, .55f), lw * .35);
    }
}

void brow(Canvas& c, const Face& f, int side) {
    const double cx = X(side * (kEyeX + .02)), cy = Y(kEyeZ + kEyeH + .13);
    const double w = S(.12), lw = S(.03);
    const double in = -side;  // toward the nose
    double yin = 0, yout = 0, lift = 0;
    switch (f.brow) {
        case Brow::neutral: break;
        case Brow::raised: lift = S(.06); break;
        case Brow::angry: yin = S(.06); yout = -S(.04); break;
        case Brow::worried: yin = -S(.06); yout = S(.03); break;
    }
    arc_stroke(c, cx + in * w, cy + yin - lift, cx, cy - S(.035) - lift, cx - in * w, cy + yout - lift, kBrowCol, lw);
}

void mouth(Canvas& c, const Face& f) {
    const double cx = X(0), cy = Y(-.56);
    const double lw = S(.025);
    auto open_shape = [&](double w, double top, double bot, bool tongue_out) {
        c.begin(); c.move(cx - w, cy + top); c.quad(cx, cy + top + S(.02), cx + w, cy + top); c.quad(cx + w * .6, cy + bot, cx, cy + bot);
        c.quad(cx - w * .6, cy + bot, cx - w, cy + top); c.close();
        c.fill(kMouthIn);
        c.begin(); c.ellipse(cx + w * .1, cy + bot - (bot - top) * .22, w * .55, (bot - top) * .3); c.fill(kTongue);
        if (tongue_out) {
            c.begin(); c.move(cx - w * .45, cy + bot - S(.01)); c.quad(cx - w * .5, cy + bot + S(.09), cx, cy + bot + S(.09));
            c.quad(cx + w * .5, cy + bot + S(.09), cx + w * .45, cy + bot - S(.01)); c.close(); c.fill(kTongue);
            c.stroke_line(cx, cy + bot, cx, cy + bot + S(.05), alpha(kLine, .5f), lw * .5);
        }
        c.begin(); c.move(cx - w, cy + top); c.quad(cx, cy + top + S(.02), cx + w, cy + top); c.quad(cx + w * .6, cy + bot, cx, cy + bot);
        c.quad(cx - w * .6, cy + bot, cx - w, cy + top); c.close(); c.stroke(kLine, lw * .8);
    };
    switch (f.mouth) {
        case Mouth::smile: arc_stroke(c, cx - S(.08), cy - S(.01), cx, cy + S(.06), cx + S(.08), cy - S(.01), kLine, lw); break;
        case Mouth::cat:  // ω
            arc_stroke(c, cx - S(.1), cy - S(.01), cx - S(.05), cy + S(.07), cx, cy, kLine, lw);
            arc_stroke(c, cx, cy, cx + S(.05), cy + S(.07), cx + S(.1), cy - S(.01), kLine, lw);
            break;
        case Mouth::open_smile: open_shape(S(.11), -S(.02), S(.11), false); break;
        case Mouth::grin_tongue: open_shape(S(.12), -S(.03), S(.1), true); break;
        case Mouth::pout:  // a small puckered 3
            c.begin(); c.move(cx - S(.03), cy - S(.04)); c.quad(cx + S(.05), cy - S(.03), cx, cy); c.quad(cx + S(.05), cy + S(.03), cx - S(.03), cy + S(.04));
            c.stroke(kLine, lw);
            break;
        case Mouth::frown: arc_stroke(c, cx - S(.08), cy + S(.04), cx, cy - S(.04), cx + S(.08), cy + S(.04), kLine, lw); break;
        case Mouth::wavy: {
            c.begin();
            for (int i = 0; i <= 6; ++i) {
                const double px = cx - S(.12) + i * S(.04), py = cy + (i % 2 ? -S(.025) : S(.025));
                i == 0 ? c.move(px, py) : c.line(px, py);
            }
            c.stroke(kLine, lw * .9);
            break;
        }
        case Mouth::shout: open_shape(S(.15), -S(.06), S(.16), false); break;
        case Mouth::o_small: c.begin(); c.ellipse(cx, cy + S(.02), S(.04), S(.05)); c.fill(kMouthIn); c.begin(); c.ellipse(cx, cy + S(.02), S(.04), S(.05)); c.stroke(kLine, lw * .7); break;
        case Mouth::flat: c.stroke_line(cx - S(.06), cy + S(.01), cx + S(.06), cy + S(.01), kLine, lw); break;
        case Mouth::bleh:
            arc_stroke(c, cx - S(.08), cy - S(.01), cx, cy + S(.05), cx + S(.08), cy - S(.01), kLine, lw);
            c.begin(); c.move(cx + S(.0), cy + S(.03)); c.quad(cx + S(.0), cy + S(.12), cx + S(.05), cy + S(.12)); c.quad(cx + S(.1), cy + S(.12), cx + S(.08), cy + S(.02));
            c.close(); c.fill(kTongue);
            break;
    }
}

void blush(Canvas& c, const Face& f) {
    if (f.blush <= 0) return;
    for (int side = -1; side <= 1; side += 2) {
        const double cx = X(side * .52), cy = Y(-.42);
        const double rx = S(f.blush > 1 ? .15 : .12), ry = S(f.blush > 1 ? .08 : .06);
        c.begin(); c.ellipse(cx, cy, rx, ry);
        c.fill(Paint::rad(cx, cy, rx, {{0, alpha(kBlush, f.blush > 1 ? .85f : .6f)}, {1, alpha(kBlush, 0)}}));
        if (f.blush > 1)
            for (int k = -1; k <= 1; ++k)
                c.stroke_line(cx + k * S(.05) - S(.02), cy + S(.035), cx + k * S(.05) + S(.02), cy - S(.035), hex(0xE0606A), S(.014));
    }
}

void paint(Canvas& c, const Face& f) {
    c.clear(kSkin);
    blush(c, f);
    // a tiny nose
    c.fill_ellipse(X(0), Y(-.34), S(.012), S(.008), hex(0xE7B9A6));
    for (int side = -1; side <= 1; side += 2) { eye(c, f, side); brow(c, f, side); }
    mouth(c, f);
}
}  // namespace

Col skin_col() { return kSkin; }

const Tex& face_texture(const Face& f) {
    static std::unordered_map<std::uint32_t, Tex> cache;
    auto it = cache.find(f.key());
    if (it != cache.end()) return it->second;
    static Canvas c;
    c.resize(kTex, kTex);
    paint(c, f);
    Tex t;
    t.make(kTex, kTex);
    for (size_t i = 0; i < t.px.size(); ++i) {
        const std::uint8_t* p = &c.px[i * 4];  // BGRA premultiplied, opaque here
        t.px[i] = 0xFF000000u | static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[1]) << 8 | p[0];
    }
    t.build_mips();
    if (cache.size() > 400) cache.clear();
    return cache.emplace(f.key(), std::move(t)).first->second;
}

}  // namespace sbx
