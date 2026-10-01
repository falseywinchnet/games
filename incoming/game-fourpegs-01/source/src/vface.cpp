#include "vface.hpp"

#include "platform/raster.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace fp {

namespace {
constexpr int kTex = 256;
// a composed gentleman: natural complexion, cold grey eyes, restrained features
const Col kSkin = hex(0xE6CDB8), kShade = hex(0xBE9C86), kLine = hex(0x8E6A5A), kInk = hex(0x2A2024), kBrowCol = hex(0x3A3336);
const Col kIris = hex(0x7E95A8), kIrisRim = hex(0x34434F), kWhite = hex(0xFBF7F2);
const Col kLip = hex(0xA9716A), kLipDark = hex(0x6E4440), kMouthIn = hex(0x4A2428), kTeeth = hex(0xF7F2EA);

double X(double x) { return kTex * (.5 + x * kVFaceK); }
double Y(double z) { return kTex * (.5 - (z - kVFaceZ) * kVFaceK); }
double S(double d) { return d * kTex * kVFaceK; }

constexpr double kEyeX = .33, kEyeZ = .04, kEyeW = .17, kEyeH = .095;

struct Pt2 { double x, y; };

void quad_pts(std::vector<Pt2>& out, Pt2 a, Pt2 c, Pt2 b, int n = 10) {
    for (int i = 1; i <= n; ++i) {
        const double t = static_cast<double>(i) / n, u = 1 - t;
        out.push_back({u * u * a.x + 2 * u * t * c.x + t * t * b.x, u * u * a.y + 2 * u * t * c.y + t * t * b.y});
    }
}

void poly(Canvas& c, const std::vector<Pt2>& p, bool reverse) {
    const int n = static_cast<int>(p.size());
    for (int i = 0; i < n; ++i) {
        const Pt2& q = p[static_cast<size_t>(reverse ? n - 1 - i : i)];
        i == 0 ? c.move(q.x, q.y) : c.line(q.x, q.y);
    }
    c.close();
}

double signed_area(const std::vector<Pt2>& p) {
    double a = 0;
    for (size_t i = 0; i < p.size(); ++i) a += p[i].x * p[(i + 1) % p.size()].y - p[(i + 1) % p.size()].x * p[i].y;
    return a;
}

void qline(Canvas& c, double x0, double y0, double cx, double cy, double x1, double y1, Col col, double w) {
    c.begin(); c.move(x0, y0); c.quad(cx, cy, x1, y1); c.stroke(col, w);
}

// One eye; side -1 is screen left.
void eye(Canvas& c, const VFace& f, int side) {
    const double cx = X(side * kEyeX), cy = Y(kEyeZ);
    const double w = S(kEyeW), h = S(kEyeH);
    const double lw = S(.028);
    const double in = -side;
    VEyes e = f.eyes;
    if (e == VEyes::twitch) e = side < 0 ? VEyes::squint : VEyes::menace;
    if (e == VEyes::closed) {  // contemplation: calm, slightly downturned lids
        qline(c, cx - w, cy, cx, cy + h * .7, cx + w, cy, kInk, lw);
        qline(c, cx - w * .7, cy + h * 1.1, cx, cy + h * 1.35, cx + w * .7, cy + h * 1.1, alpha(kLine, .5f), lw * .5);
        return;
    }
    if (e == VEyes::laugh) {  // a quiet, closed-eyed chuckle
        qline(c, cx - w, cy + h * .2, cx, cy - h * .7, cx + w, cy + h * .2, kInk, lw);
        qline(c, cx - in * w * 1.05, cy + h * .1, cx - in * w * 1.25, cy + h * .4, cx - in * w * 1.2, cy + h * .75, alpha(kLine, .6f), lw * .45);
        return;
    }
    double top = h, bot = h * .7, dip = 0;
    if (e == VEyes::half) top = h * .45;
    if (e == VEyes::wide) { top = h * 1.4; bot = h * 1.1; }
    if (e == VEyes::furious) { top = h * .75; dip = .6; }
    if (e == VEyes::squint) { top = h * .5; bot = h * .45; }
    if (e == VEyes::glint) { top = h * .8; dip = .25; }
    const Pt2 inner{cx + in * w, cy + h * .15}, outer{cx - in * w, cy};
    std::vector<Pt2> almond{inner};
    quad_pts(almond, inner, {cx + in * w * .15, cy - top * (1.7 - dip)}, outer);
    quad_pts(almond, outer, {cx, cy + bot * 1.5}, inner);
    if (signed_area(almond) < 0) std::reverse(almond.begin(), almond.end());
    c.begin(); poly(c, almond, false); c.fill(kWhite);
    const double ix = cx + f.look_x * w * .2, iy = cy - f.look_y * h * .3 + h * .05;
    const double ir = .5 * std::min(w, h * 2.0);
    c.begin(); c.circle(ix, iy, ir); c.fill(Paint::rad(ix, iy - ir * .2, ir, {{0, mix(kIris, kWhite, .25f)}, {.6f, kIris}, {1, kIrisRim}}));
    c.fill_circle(ix, iy, ir * (e == VEyes::wide ? .28 : .38), hex(0x101014));
    c.fill_circle(ix - ir * .35, iy - ir * .38, ir * (e == VEyes::glint ? .3 : .18), alpha(kWhite, .95f));
    // skin over everything outside the almond
    c.begin();
    c.move(cx - w * 1.6, cy - h * 3.2); c.line(cx + w * 1.6, cy - h * 3.2); c.line(cx + w * 1.6, cy + h * 3); c.line(cx - w * 1.6, cy + h * 3); c.close();
    poly(c, almond, true);
    c.fill(kSkin);
    // a firm upper lid, a crease above it, a faint lower lid, and the fine lines of experience
    c.begin(); c.move(inner.x, inner.y); c.quad(cx + in * w * .15, cy - top * (1.7 - dip), outer.x, outer.y); c.stroke(kInk, lw);
    qline(c, inner.x - in * w * .1, inner.y - h * .9, cx, cy - top * 1.75 - h * .35, outer.x + in * w * .1, outer.y - h * .75, alpha(kLine, .55f), lw * .45);
    qline(c, inner.x - in * w * .2, inner.y + bot * .5, cx, cy + bot * 1.15, outer.x + in * w * .15, outer.y + bot * .4, alpha(kLine, .45f), lw * .4);
    qline(c, outer.x - in * w * .08, outer.y + h * .1, outer.x - in * w * .22, outer.y + h * .2, outer.x - in * w * .28, outer.y + h * .45, alpha(kLine, .4f), lw * .35);
}

void brow(Canvas& c, const VFace& f, int side) {
    const double cx = X(side * (kEyeX + .01)), cy = Y(kEyeZ + .2);
    const double w = S(.22), lw = S(.055);
    const double in = -side;
    double yin = 0, yout = 0, mid = -S(.025);
    switch (f.brow) {
        case VBrow::flat: break;
        case VBrow::arched: yin = -S(.04); yout = -S(.02); mid = -S(.07); break;
        case VBrow::furious: yin = S(.07); yout = -S(.02); mid = S(.01); break;
        case VBrow::worried: yin = -S(.06); yout = S(.03); break;
        case VBrow::quizzical: if (side > 0) { yin = -S(.06); yout = -S(.03); mid = -S(.09); } break;
    }
    // a neat, straight, slightly tapered brow
    c.begin();
    c.move(cx + in * w * .55, cy + yin);
    c.quad(cx, cy + mid, cx - in * w * .6, cy + yout + S(.01));
    c.quad(cx, cy + mid + lw * .8, cx + in * w * .55, cy + yin + lw);
    c.close();
    c.fill(kBrowCol);
}

void mouth(Canvas& c, const VFace& f) {
    const double cx = X(0), cy = Y(-.5);
    const double lw = S(.026);
    auto lips_open = [&](double w, double top, double bot, bool teeth) {
        c.begin();
        c.move(cx - w, cy); c.quad(cx, cy + top, cx + w, cy); c.quad(cx, cy + bot, cx - w, cy); c.close();
        c.fill(kMouthIn);
        if (teeth) { c.begin(); c.move(cx - w * .75, cy + top * .6); c.quad(cx, cy + top * 1.05, cx + w * .75, cy + top * .6); c.line(cx + w * .7, cy + top * .3 + S(.03)); c.line(cx - w * .7, cy + top * .3 + S(.03)); c.close(); c.fill(kTeeth); }
        c.begin(); c.move(cx - w, cy); c.quad(cx, cy + top, cx + w, cy); c.quad(cx, cy + bot, cx - w, cy); c.close(); c.stroke(kLipDark, lw * .7);
        qline(c, cx - w * .6, cy + bot + S(.03), cx, cy + bot + S(.05), cx + w * .6, cy + bot + S(.03), alpha(kLip, .6f), lw * .6);
    };
    switch (f.mouth) {
        case VMouth::smirk: qline(c, cx - S(.13), cy + S(.01), cx + S(.02), cy + S(.03), cx + S(.14), cy - S(.03), kLipDark, lw); break;
        case VMouth::grin: qline(c, cx - S(.15), cy - S(.02), cx, cy + S(.05), cx + S(.15), cy - S(.02), kLipDark, lw); break;  // a polite, knowing smile
        case VMouth::sneer: qline(c, cx - S(.13), cy + S(.02), cx + S(.03), cy + S(.02), cx + S(.12), cy - S(.04), kLipDark, lw * 1.1); break;
        case VMouth::flat: c.stroke_line(cx - S(.12), cy + S(.01), cx + S(.12), cy + S(.01), kLipDark, lw); break;
        case VMouth::laugh: lips_open(S(.13), -S(.03), S(.08), true); break;      // a restrained chuckle
        case VMouth::shout: lips_open(S(.11), -S(.035), S(.1), true); break;      // speaking firmly
        case VMouth::gasp: c.begin(); c.ellipse(cx, cy + S(.03), S(.045), S(.06)); c.fill(kMouthIn); break;
        case VMouth::frown: qline(c, cx - S(.13), cy + S(.04), cx, cy - S(.02), cx + S(.13), cy + S(.04), kLipDark, lw); break;
        case VMouth::grimace: c.stroke_line(cx - S(.13), cy + S(.01), cx + S(.13), cy + S(.01), kLipDark, lw * 1.4); break;  // tight-lipped
        case VMouth::purse: c.begin(); c.ellipse(cx, cy + S(.01), S(.05), S(.025)); c.fill(kLip); break;
    }
    // a hint of lower lip beneath closed mouths
    if (f.mouth == VMouth::smirk || f.mouth == VMouth::grin || f.mouth == VMouth::flat || f.mouth == VMouth::frown || f.mouth == VMouth::grimace)
        qline(c, cx - S(.06), cy + S(.07), cx, cy + S(.09), cx + S(.06), cy + S(.07), alpha(kLip, .55f), lw * .7);
}

void paint(Canvas& c, const VFace& f) {
    c.clear(kSkin);
    // sculpting: cheekbones, the nose, and the lines beside the mouth
    for (int s = -1; s <= 1; s += 2) {
        // hollows beneath the cheekbones, set in shadow eye sockets, the lines of a stern mouth
        c.begin(); c.move(X(s * .62), Y(-.12)); c.quad(X(s * .4), Y(-.3), X(s * .3), Y(-.5)); c.quad(X(s * .5), Y(-.32), X(s * .66), Y(-.2)); c.close();
        c.fill(alpha(kShade, .3f));
        c.begin(); c.ellipse(X(s * kEyeX), Y(kEyeZ + .06), S(.24), S(.12)); c.fill(Paint::rad(X(s * kEyeX), Y(kEyeZ + .06), S(.24), {{0, alpha(kShade, .7f)}, {1, alpha(kShade, 0)}}));
        qline(c, X(s * .16), Y(-.32), X(s * .21), Y(-.42), X(s * .19), Y(-.52), alpha(kLine, .3f), S(.012));
    }
    qline(c, X(.035), Y(-.04), X(.05), Y(-.2), X(.06), Y(-.27), alpha(kLine, .55f), S(.016));          // the bridge
    qline(c, X(-.07), Y(-.3), X(0), Y(-.335), X(.07), Y(-.3), alpha(kLine, .7f), S(.016));             // the tip
    for (int side = -1; side <= 1; side += 2) { eye(c, f, side); brow(c, f, side); }
    // an old duelling scar down through his left brow (screen right), past the eye, onto the cheek
    {
        const double x0 = X(.22), y0 = Y(.42), cx = X(.44), cy = Y(.12), x1 = X(.5), y1 = Y(-.22);
        c.begin(); c.ellipse(X(.3), Y(.25), S(.035), S(.05)); c.fill(kSkin);  // the brow parts where the blade went
        qline(c, x0, y0, cx, cy, x1, y1, alpha(kLine, .45f), S(.05));
        qline(c, x0, y0, cx, cy, x1, y1, hex(0xF3DFD3), S(.026));
        for (int k = 1; k <= 4; ++k) {  // faint stitch marks
            const double u = k / 5.0, w = 1 - u;
            const double px = w * w * x0 + 2 * w * u * cx + u * u * x1, py = w * w * y0 + 2 * w * u * cy + u * u * y1;
            c.stroke_line(px - S(.03), py - S(.012), px + S(.03), py + S(.012), alpha(kLine, .4f), S(.01));
        }
    }
    mouth(c, f);
}
}  // namespace

Col villain_skin() { return kSkin; }

const Tex& vface_texture(const VFace& f) {
    static std::unordered_map<std::uint32_t, Tex> cache;
    auto it = cache.find(f.key());
    if (it != cache.end()) return it->second;
    static Canvas c;
    c.resize(kTex, kTex);
    paint(c, f);
    Tex t;
    t.make(kTex, kTex);
    for (size_t i = 0; i < t.px.size(); ++i) {
        const std::uint8_t* p = &c.px[i * 4];
        t.px[i] = 0xFF000000u | static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[1]) << 8 | p[0];
    }
    t.build_mips();
    if (cache.size() > 500) cache.clear();
    return cache.emplace(f.key(), std::move(t)).first->second;
}

}  // namespace fp
