#include "crane_model.hpp"

#include "shapes.hpp"
#include "run.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace zc {

namespace {

constexpr double kPi = 3.14159265358979323846;

const Col kPaint{0.98f, 0.74f, 0.08f, 1};
const Col kPaintDeep{0.90f, 0.62f, 0.05f, 1};
const Col kPaintLight{1.00f, 0.80f, 0.20f, 1};
const Col kBlackPaint{0.10f, 0.10f, 0.11f, 1};
const Col kRubber{0.085f, 0.085f, 0.09f, 1};
const Col kSteel{0.20f, 0.20f, 0.22f, 1};
const Col kChrome{0.82f, 0.84f, 0.88f, 1};
const Col kCable{0.18f, 0.18f, 0.20f, 1};
const Col kGlass{0.62f, 0.78f, 0.88f, 0.30f};
const Col kInterior{0.24f, 0.22f, 0.22f, 1};
const Col kSeat{0.36f, 0.20f, 0.14f, 1};
const Col kHookRed{0.82f, 0.16f, 0.10f, 1};
// the duck
const Col kDuck{1.00f, 0.86f, 0.18f, 1};
const Col kDuckWing{0.97f, 0.78f, 0.12f, 1};
const Col kBill{1.00f, 0.47f, 0.08f, 1};
const Col kDuckHat{0.93f, 0.30f, 0.10f, 1};

V3 at(const M34& frame, double x, double y, double z) {
    return frame.apply(V3{x, y, z});
}

// a flat quad given in a frame's own coordinates
void quad_in(std::vector<Vtx>& out, const M34& frame, V3 a, V3 b, V3 c, V3 d, const double st[4][2], V3 outward, Col colour) {
    const M34 normals = frame.normal_matrix();
    add_quad(out, frame.apply(a), frame.apply(b), frame.apply(c), frame.apply(d), st, norm(normals.dir(outward)), colour);
}

// a pane of glass in a frame: the rectangle spanned by `corner`, `along` and
// `up`, facing `outward`
void pane(std::vector<Vtx>& out, const M34& frame, V3 corner, V3 along, V3 up, V3 outward) {
    const double st[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    quad_in(out, frame, corner, corner + along, corner + along + up, corner + up, st, outward, kGlass);
}

// the stripes on a flat panel: texture coordinates along two of its axes
void stripe_panel(std::vector<Vtx>& out, const M34& frame, V3 corner, V3 along, V3 up, V3 outward) {
    const double period = 0.022;   // metres per repeat of the stripe texture
    const double s1 = len(along) / period;
    const double t1 = len(up) / period;
    const double st[4][2] = {{0, 0}, {s1, 0}, {s1, t1}, {0, t1}};
    quad_in(out, frame, corner, corner + along, corner + along + up, corner + up, st, outward, Col{1, 1, 1, 1});
}

void lathe_in(std::vector<Vtx>& out, const M34& frame, const double* r, const double* z, int count, int slices, Col colour) {
    add_lathe(out, frame, r, z, count, slices, colour);
}

// a closed cylinder (both ends capped) along the frame's z axis
void add_can(std::vector<Vtx>& out, const M34& frame, double radius, double z0, double z1, int slices, Col colour) {
    const double r[4] = {0, radius, radius, 0};
    const double z[4] = {z0, z0, z1, z1};
    add_lathe(out, frame, r, z, 4, slices, colour);
}

void make_stripes(Tex& t) {
    t.make(64, 64);
    for (int y = 0; y < 64; y += 1) {
        for (int x = 0; x < 64; x += 1) {
            const bool black = ((x + y) / 16) % 2 == 1;
            const double edge = ((x + y) % 16 == 0) ? 0.85 : 1.0;   // a hint of a painted edge
            const double r = black ? 0.10 : 0.98 * edge;
            const double g = black ? 0.10 : 0.74 * edge;
            const double b = black ? 0.11 : 0.08 * edge;
            const std::uint32_t pr = static_cast<std::uint32_t>(r * 255);
            const std::uint32_t pg = static_cast<std::uint32_t>(g * 255);
            const std::uint32_t pb = static_cast<std::uint32_t>(b * 255);
            t.at(x, y) = 0xFF000000u | (pr << 16) | (pg << 8) | pb;
        }
    }
    t.build_mips();
}

// pressed black steel with a round hole in the middle of each plate, the rim
// of the hole turned in a little (lighter where it catches the light)
void make_plate(Tex& t) {
    t.make(64, 64);
    for (int y = 0; y < 64; y += 1) {
        for (int x = 0; x < 64; x += 1) {
            const double dx = x + 0.5 - 32;
            const double dy = y + 0.5 - 32;
            const double r = std::sqrt(dx * dx + dy * dy);
            std::uint32_t pixel = 0xFF1C1C1Fu;
            if (r < 19) {
                pixel = 0x00000000u;
            } else if (r < 23) {
                // the lip: lit from above, shaded below
                const double light = 0.5 - 0.5 * dy / r;
                const std::uint32_t v = static_cast<std::uint32_t>(30 + 40 * light);
                pixel = 0xFF000000u | (v << 16) | (v << 8) | (v + 3);
            }
            t.at(x, y) = pixel;
        }
    }
    t.build_mips();
}

}  // namespace

CraneModel::CraneModel() {
    make_stripes(stripe_tex);
    make_plate(plate_tex);
}

void CraneModel::set_setup(const CraneSetup& setup) {
    setup_ = setup;
    build_truck();
    built_ = true;
}

// ---------------------------------------------------------------- the truck

// A wheel in its own frame: the axle along z, outward +z.
void CraneModel::add_wheel(const M34& frame) {
    // the tyre: a rounded carcass with chevron lugs round the tread
    {
        const double r[8] = {0.031, 0.040, 0.0445, 0.0465, 0.0465, 0.0445, 0.040, 0.031};
        const double z[8] = {-0.0165, -0.0180, -0.0165, -0.0120, 0.0120, 0.0165, 0.0180, 0.0165};
        lathe_in(truck_matte_, frame, r, z, 8, 28, kRubber);
    }
    const int lugs = 14;
    for (int k = 0; k < lugs; k += 1) {
        const double angle = 2 * kPi * k / lugs;
        for (int half = -1; half <= 1; half += 2) {
            const M34 lug = frame * M34::rot_z(angle + half * 0.06) * M34::translate(0.0455, 0, half * 0.0068) * M34::rot_x(half * 0.5) * M34::rot_y(kPi / 2);
            add_box(truck_matte_, lug, V3{-0.0060, -0.0042, 0}, V3{0.0060, 0.0042, 0.0032}, kRubber);
        }
    }
    // the deep-dished hub, its chrome cap and nuts
    {
        const double r[6] = {0.0305, 0.0300, 0.0285, 0.0255, 0.0175, 0.0140};
        const double z[6] = {0.0150, 0.0170, 0.0172, 0.0130, 0.0085, 0.0085};
        lathe_in(truck_paint_, frame, r, z, 6, 20, kPaint);
        const double cr[5] = {0.0140, 0.0125, 0.0090, 0.0050, 0.0};
        const double cz[5] = {0.0085, 0.0110, 0.0135, 0.0150, 0.0155};
        lathe_in(truck_paint_, frame, cr, cz, 5, 14, kChrome);
        for (int n = 0; n < 5; n += 1) {
            const double a = 2 * kPi * n / 5 + 0.3;
            const M34 nut = frame * M34::translate(0.0158 * std::cos(a), 0.0158 * std::sin(a), 0);
            const double nr[3] = {0.0019, 0.0019, 0.0};
            const double nz[3] = {0.0085, 0.0108, 0.0108};
            lathe_in(truck_paint_, nut, nr, nz, 3, 6, kChrome);
        }
        const double ir[2] = {0.0, 0.031};
        const double iz[2] = {-0.016, -0.016};
        lathe_in(truck_matte_, frame, ir, iz, 2, 14, kSteel);
    }
}

// The cab-over cab: painted lower body, pillars and roof, glass all round, a
// grille, lamps, mirrors and wipers, and inside the seats, the dash and the
// steering wheel the duck holds.
void CraneModel::add_cab() {
    const M34& c = setup_.chassis;
    add_rounded_box(truck_paint_, c, V3{0.098, -0.070, 0.106}, V3{0.214, 0.070, 0.131}, 0.007, 2, kPaint);
    // the apron below it, between the front wheels, with the grille and lamps
    add_rounded_box(truck_paint_, c, V3{0.186, -0.061, 0.058}, V3{0.213, 0.061, 0.110}, 0.005, 2, kPaint);
    add_box(truck_matte_, c, V3{0.2105, -0.033, 0.067}, V3{0.2148, 0.033, 0.101}, kBlackPaint);
    for (int bar = 0; bar < 5; bar += 1) {
        const double z = 0.0705 + 0.0068 * bar;
        add_rod(truck_paint_, at(c, 0.2156, -0.032, z), at(c, 0.2156, 0.032, z), 0.0013, kChrome, 5);
    }
    add_rod(truck_paint_, at(c, 0.2156, -0.034, 0.066), at(c, 0.2156, -0.034, 0.102), 0.0015, kChrome, 5);
    add_rod(truck_paint_, at(c, 0.2156, 0.034, 0.066), at(c, 0.2156, 0.034, 0.102), 0.0015, kChrome, 5);
    for (int side = -1; side <= 1; side += 2) {
        const M34 lamp = c * M34::translate(0.2125, side * 0.047, 0.086) * M34::rot_y(kPi / 2);
        const double br[4] = {0.0102, 0.0102, 0.0084, 0.0080};
        const double bz[4] = {0.0, 0.0032, 0.0038, 0.0036};
        lathe_in(truck_paint_, lamp, br, bz, 4, 14, kChrome);
        const double lr[3] = {0.0080, 0.0056, 0.0};
        const double lz[3] = {0.0036, 0.0048, 0.0052};
        lathe_in(truck_paint_, lamp, lr, lz, 3, 14, Col{0.95f, 0.95f, 0.88f, 1});
        // indicator below the headlamp
        add_rounded_box(truck_lamps_, c, V3{0.2125, side * 0.047 - 0.006, 0.066}, V3{0.2150, side * 0.047 + 0.006, 0.071}, 0.001, 1, Col{0.95f, 0.55f, 0.12f, 1});
    }
    // the bumper, black, and its tow hooks
    add_rounded_box(truck_paint_, c, V3{0.211, -0.078, 0.044}, V3{0.226, 0.078, 0.060}, 0.005, 2, kBlackPaint);
    for (int side = -1; side <= 1; side += 2) {
        add_arc_tube(truck_paint_, c * M34::translate(0.229, side * 0.036, 0.052) * M34::rot_x(kPi / 2), 0.0042, 0.0011, -kPi / 2, kPi / 2, 6, 4, kHookRed);
    }
    // pillars and roof
    const double pillar_x[3][2] = {{0.205, 0.213}, {0.099, 0.107}, {0.1505, 0.1550}};
    for (int p = 0; p < 3; p += 1) {
        for (int side = -1; side <= 1; side += 2) {
            const double y0 = side > 0 ? 0.061 : -0.069;
            const double y1 = side > 0 ? 0.069 : -0.061;
            add_box(truck_paint_, c, V3{pillar_x[p][0], y0, 0.129}, V3{pillar_x[p][1], y1, 0.172}, kPaint);
        }
    }
    add_box(truck_paint_, c, V3{0.2075, -0.0025, 0.129}, V3{0.2130, 0.0025, 0.172}, kPaint);
    add_rounded_box(truck_paint_, c, V3{0.095, -0.073, 0.169}, V3{0.217, 0.073, 0.180}, 0.0045, 2, kPaint);
    // a sun visor over the windscreen and marker lamps on the roof's front edge
    add_box(truck_paint_, c, V3{0.212, -0.066, 0.1665}, V3{0.221, 0.066, 0.1690}, kPaintDeep);
    for (int m = -1; m <= 1; m += 1) {
        add_rounded_box(truck_lamps_, c, V3{0.214, m * 0.022 - 0.004, 0.1795}, V3{0.2185, m * 0.022 + 0.004, 0.1835}, 0.0012, 1,
                        Col{0.98f, 0.60f, 0.12f, 1});
    }
    // glass: windscreen, the doors' windows, the back
    pane(truck_glass_, c, V3{0.2102, -0.0615, 0.130}, V3{0, 0.123, 0}, V3{0, 0, 0.040}, V3{1, 0, 0});
    pane(truck_glass_, c, V3{0.106, 0.0660, 0.130}, V3{0.100, 0, 0}, V3{0, 0, 0.040}, V3{0, 1, 0});
    pane(truck_glass_, c, V3{0.106, -0.0660, 0.130}, V3{0.100, 0, 0}, V3{0, 0, 0.040}, V3{0, -1, 0});
    pane(truck_glass_, c, V3{0.1015, -0.0615, 0.130}, V3{0, 0.123, 0}, V3{0, 0, 0.040}, V3{-1, 0, 0});
    // wipers resting at the foot of the windscreen
    for (int side = -1; side <= 1; side += 2) {
        add_rod(truck_matte_, at(c, 0.2112, side * 0.050, 0.1315), at(c, 0.2112, side * 0.012, 0.1365), 0.0008, kBlackPaint, 3);
    }
    // door seams and handles
    for (int side = -1; side <= 1; side += 2) {
        const double y0 = side > 0 ? 0.0700 : -0.0708;
        const double y1 = side > 0 ? 0.0708 : -0.0700;
        add_box(truck_matte_, c, V3{0.1520, y0, 0.108}, V3{0.1532, y1, 0.130}, kSteel);
        add_box(truck_paint_, c, V3{0.1555, y0 + side * 0.0004, 0.1225}, V3{0.1625, y1 + side * 0.0010, 0.1250}, kChrome);
    }
    // mirrors on arms
    for (int side = -1; side <= 1; side += 2) {
        add_rod(truck_paint_, at(c, 0.205, side * 0.069, 0.160), at(c, 0.209, side * 0.090, 0.162), 0.0012, kChrome, 4);
        add_rod(truck_paint_, at(c, 0.205, side * 0.069, 0.137), at(c, 0.209, side * 0.090, 0.149), 0.0010, kChrome, 4);
        add_rounded_box(truck_paint_, c, V3{0.2055, side * 0.091 - 0.0035, 0.144}, V3{0.2105, side * 0.091 + 0.0035, 0.166}, 0.0015, 1, kBlackPaint);
    }
    // inside: a dark floor, the dash, two bench seats, the wheel and its column
    add_box(truck_matte_, c, V3{0.103, -0.064, 0.1310}, V3{0.209, 0.064, 0.1322}, kInterior);
    add_rounded_box(truck_matte_, c, V3{0.190, -0.062, 0.131}, V3{0.207, 0.062, 0.141}, 0.003, 1, kInterior);
    add_rounded_box(truck_lamps_, c, V3{0.1890, -0.050, 0.1375}, V3{0.1905, -0.040, 0.1400}, 0.0005, 1, Col{0.45f, 0.90f, 0.55f, 1});
    for (int side = -1; side <= 1; side += 2) {
        const double y0 = side > 0 ? 0.014 : -0.058;
        const double y1 = side > 0 ? 0.058 : -0.014;
        add_rounded_box(truck_matte_, c, V3{0.116, y0, 0.131}, V3{0.152, y1, 0.139}, 0.003, 1, kSeat);
        add_rounded_box(truck_matte_, c, V3{0.108, y0, 0.135}, V3{0.118, y1, 0.166}, 0.003, 1, kSeat);
    }
    {
        const M34 wheel = c * M34::translate(0.178, -0.036, 0.151) * M34::rot_y(-1.0);
        add_arc_tube(truck_matte_, wheel, 0.0092, 0.0014, 0, 2 * kPi, 16, 4, kBlackPaint);
        add_rod(truck_matte_, at(wheel, -0.0092, 0, 0), at(wheel, 0.0092, 0, 0), 0.0009, kBlackPaint, 3);
        add_rod(truck_matte_, at(c, 0.178, -0.036, 0.151), at(c, 0.197, -0.036, 0.139), 0.0016, kBlackPaint, 4);
    }
    // the exhaust behind the cab, chrome with a black heat shield
    add_rod(truck_paint_, at(c, 0.091, -0.058, 0.088), at(c, 0.091, -0.058, 0.200), 0.0040, kChrome, 8);
    {
        const M34 shield = c * M34::translate(0.091, -0.058, 0);
        const double sr[4] = {0.0040, 0.0058, 0.0058, 0.0040};
        const double sz[4] = {0.126, 0.127, 0.168, 0.169};
        lathe_in(truck_paint_, shield, sr, sz, 4, 8, kBlackPaint);
        add_mesh(truck_paint_, disc_mesh(8), c * M34::translate(0.091, -0.058, 0.2005) * M34::rot_y(-0.35) * M34::scale(0.0048, 0.0048, 1), kBlackPaint);
    }
    // an air cleaner can on the other side
    add_can(truck_paint_, c * M34::translate(0.091, 0.056, 0), 0.0085, 0.092, 0.128, 10, kBlackPaint);
}

// An outrigger: a beam out of its box, a ram down to a pad on the ground
// (pad 3 hangs a finger's width short).
void CraneModel::add_outrigger(int index, double x, double side) {
    const M34& c = setup_.chassis;
    const double y_in = side * 0.064;
    const double y_out = side * 0.146;
    add_rounded_box(truck_paint_, c, V3{x - 0.0085, std::min(y_in, y_out), 0.040}, V3{x + 0.0085, std::max(y_in, y_out), 0.056}, 0.002, 1, kPaint);
    stripe_panel(truck_striped_, c, V3{x - 0.0085, y_out + side * 0.0012, 0.040}, V3{0.017, 0, 0}, V3{0, 0, 0.016}, V3{0, side, 0});
    const V3 top = at(c, x, side * 0.137, 0.040);
    const V3 barrel_end = top - V3{0, 0, 0.020};
    add_rod(truck_paint_, top + V3{0, 0, 0.002}, barrel_end, 0.0075, kPaintDeep, 10);
    const double ground = setup_.pad_ground[index] + (index == 3 ? 0.012 : 0.0);
    const V3 pad{top.x, top.y, ground};
    const double rod_bottom = std::min(barrel_end.z - 0.003, ground + 0.006);
    add_rod(truck_paint_, barrel_end + V3{0, 0, 0.001}, V3{pad.x, pad.y, rod_bottom}, 0.0042, kChrome, 8);
    const double pr[6] = {0.0, 0.017, 0.018, 0.018, 0.012, 0.0045};
    const double pz[6] = {0.0, 0.0, 0.001, 0.004, 0.006, 0.006};
    lathe_in(truck_matte_, M34::translate(pad.x, pad.y, rod_bottom - 0.006), pr, pz, 6, 14, kSteel);
}

void CraneModel::build_truck() {
    truck_paint_.clear();
    truck_matte_.clear();
    truck_striped_.clear();
    truck_glass_.clear();
    truck_lamps_.clear();
    const M34& c = setup_.chassis;
    // frame rails and the deck
    add_box(truck_matte_, c, V3{-0.205, -0.048, 0.036}, V3{0.205, -0.034, 0.056}, kSteel);
    add_box(truck_matte_, c, V3{-0.205, 0.034, 0.036}, V3{0.205, 0.048, 0.056}, kSteel);
    add_rounded_box(truck_paint_, c, V3{-0.216, -0.064, 0.054}, V3{0.100, 0.064, 0.089}, 0.006, 2, kPaint);
    // wheels under fenders
    const double wheel_x[2] = {0.137, -0.125};
    for (int w = 0; w < 4; w += 1) {
        const double wx = wheel_x[w / 2];
        const double side = (w % 2 == 0) ? 1 : -1;
        add_wheel(c * M34::translate(wx, side * 0.085, 0.049) * M34::rot_x(side > 0 ? -kPi / 2 : kPi / 2));
        // the fender: an arch over the tyre with a rolled edge
        const int pieces = 12;
        const double inner = 0.0555;
        const double outer = 0.0580;
        const double y0 = side * 0.060;
        const double y1 = side * 0.1065;
        const double first = 0.32;
        const double last = kPi - 0.32;
        if (wx > 0) {
            // the front ones tuck under the cab: only the back half shows
        }
        for (int p = 0; p < pieces; p += 1) {
            const double a0 = first + (last - first) * p / pieces;
            const double a1 = first + (last - first) * (p + 1) / pieces;
            const V3 r0{std::cos(a0), 0, std::sin(a0)};
            const V3 r1{std::cos(a1), 0, std::sin(a1)};
            const V3 hub{wx, 0, 0.049};
            const double st[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
            const V3 o00 = hub + r0 * outer + V3{0, y0, 0};
            const V3 o01 = hub + r1 * outer + V3{0, y0, 0};
            const V3 o11 = hub + r1 * outer + V3{0, y1, 0};
            const V3 o10 = hub + r0 * outer + V3{0, y1, 0};
            quad_in(truck_paint_, c, o00, o01, o11, o10, st, (r0 + r1) * 0.5, kPaint);
            const V3 i00 = hub + r0 * inner + V3{0, y0, 0};
            const V3 i01 = hub + r1 * inner + V3{0, y0, 0};
            const V3 i11 = hub + r1 * inner + V3{0, y1, 0};
            const V3 i10 = hub + r0 * inner + V3{0, y1, 0};
            quad_in(truck_matte_, c, i00, i01, i11, i10, st, (r0 + r1) * -0.5, kSteel);
        }
        add_arc_tube(truck_paint_, c * M34::translate(wx, y1, 0.049) * M34::rot_x(-kPi / 2), outer - 0.0012, 0.0016, -last, -first, pieces, 5, kPaint);
        // mud flaps behind the back wheels
        if (wx < 0) {
            const double fy0 = side > 0 ? 0.068 : -0.102;
            const double fy1 = side > 0 ? 0.102 : -0.068;
            add_box(truck_matte_, c, V3{-0.183, fy0, 0.018}, V3{-0.180, fy1, 0.072}, kRubber);
        }
    }
    add_cab();
    // the fuel tank on the left, chrome with black straps; a toolbox on the right
    {
        const M34 tank = c * M34::translate(-0.068, 0.080, 0.064) * M34::rot_y(kPi / 2);
        const double tr[6] = {0.0, 0.0110, 0.0125, 0.0125, 0.0110, 0.0};
        const double tz[6] = {0.0, 0.0, 0.0015, 0.1030, 0.1045, 0.1045};
        lathe_in(truck_paint_, tank, tr, tz, 6, 14, kChrome);
        for (int s = 0; s < 2; s += 1) {
            add_arc_tube(truck_matte_, tank * M34::translate(0, 0, 0.022 + 0.058 * s), 0.0127, 0.0011, 0, 2 * kPi, 14, 3, kBlackPaint);
        }
        add_rounded_box(truck_paint_, c, V3{-0.068, -0.098, 0.050}, V3{0.036, -0.064, 0.082}, 0.003, 1, kBlackPaint);
        add_box(truck_paint_, c, V3{-0.040, -0.0990, 0.074}, V3{-0.034, -0.0975, 0.079}, kChrome);
        add_box(truck_paint_, c, V3{0.008, -0.0990, 0.074}, V3{0.014, -0.0975, 0.079}, kChrome);
    }
    // outriggers: boxes across the frame, beams out, rams down
    add_box(truck_matte_, c, V3{0.060, -0.064, 0.038}, V3{0.084, 0.064, 0.058}, kSteel);
    add_box(truck_matte_, c, V3{-0.208, -0.064, 0.038}, V3{-0.184, 0.064, 0.058}, kSteel);
    add_outrigger(0, 0.072, 1);
    add_outrigger(1, 0.072, -1);
    add_outrigger(2, -0.196, 1);
    add_outrigger(3, -0.196, -1);
    // the back: bumper and lamps
    add_rounded_box(truck_paint_, c, V3{-0.229, -0.070, 0.044}, V3{-0.214, 0.070, 0.060}, 0.004, 2, kBlackPaint);
    for (int side = -1; side <= 1; side += 2) {
        add_rounded_box(truck_lamps_, c, V3{-0.2175, side * 0.051 - 0.007, 0.064}, V3{-0.2155, side * 0.051 + 0.007, 0.075}, 0.001, 1, Col{0.78f, 0.10f, 0.08f, 1});
        add_rounded_box(truck_lamps_, c, V3{-0.2175, side * 0.036 - 0.004, 0.066}, V3{-0.2155, side * 0.036 + 0.004, 0.073}, 0.001, 1, Col{0.95f, 0.55f, 0.12f, 1});
    }
    // number plates, front and back
    add_box(truck_paint_, c, V3{0.2262, -0.016, 0.0465}, V3{0.2270, 0.016, 0.0575}, Col{0.95f, 0.93f, 0.85f, 1});
    add_box(truck_paint_, c, V3{-0.2298, -0.016, 0.0465}, V3{-0.2290, 0.016, 0.0575}, Col{0.95f, 0.93f, 0.85f, 1});
    // grab handles beside the cab doors
    for (int side = -1; side <= 1; side += 2) {
        add_rod(truck_paint_, at(c, 0.101, side * 0.0715, 0.112), at(c, 0.101, side * 0.0715, 0.160), 0.0011, kChrome, 4);
    }
    // the slewing ring the house turns on
    {
        const M34 ring = c * M34::translate(-0.06, 0, 0);
        const double rr[2] = {0.060, 0.060};
        const double rz[2] = {0.089, 0.099};
        lathe_in(truck_matte_, ring, rr, rz, 2, 24, kSteel);
    }
}

// ---------------------------------------------------------------- the duck

// A rubber duck in a little orange hard hat at the wheel, looking out of the
// windows; she bobs along while the crane works and hops when it goes well.
void CraneModel::add_duck(const CranePose& pose) {
    const double t = pose.time;
    const double bob = pose.moving ? 0.0007 * std::sin(t * 9.0) : 0.0;
    const double hop = pose.mood == OperatorMood::cheering ? 0.004 * std::abs(std::sin(t * 7.0)) : 0.0;
    const double sway = 0.05 * std::sin(t * 1.3);
    const M34 duck = setup_.chassis * M34::translate(0.140, -0.036, 0.139 + bob + hop) * M34::rot_z(sway);
    add_ellipsoid(paint, duck, V3{0, 0, 0.0085}, V3{0.0115, 0.0088, 0.0085}, kDuck, 12, 8);
    add_mesh(paint, cone_mesh(8), duck * M34::translate(-0.0085, 0, 0.0105) * M34::rot_y(-0.9) * M34::scale(0.0042, 0.0042, 0.0062), kDuck);
    const double flap = pose.mood == OperatorMood::cheering ? 0.5 * std::abs(std::sin(t * 14.0)) : 0.0;
    for (int side = -1; side <= 1; side += 2) {
        const M34 wing = duck * M34::translate(-0.001, side * 0.0080, 0.0098) * M34::rot_x(side * flap);
        add_ellipsoid(paint, wing, V3{0, side * 0.0006, 0}, V3{0.0066, 0.0020, 0.0042}, kDuckWing, 8, 5);
    }
    double look = 0.5 * std::sin(t * 0.45);
    if (pose.moving) {
        look = 0.25 * std::sin(t * 0.9) + 0.12 * std::sin(t * 2.1);
    }
    const M34 head = duck * M34::translate(0.0060, 0, 0.0205) * M34::rot_z(look);
    add_ellipsoid(paint, head, V3{0, 0, 0}, V3{0.0068, 0.0068, 0.0068}, kDuck, 12, 8);
    add_ellipsoid(paint, head, V3{0.0072, 0, -0.0012}, V3{0.0046, 0.0035, 0.0014}, kBill, 10, 5);
    add_ellipsoid(paint, head, V3{0.0062, 0, -0.0025}, V3{0.0035, 0.0026, 0.0009}, kBill, 8, 4);
    for (int side = -1; side <= 1; side += 2) {
        add_ellipsoid(paint, head, V3{0.0049, side * 0.0037, 0.0020}, V3{0.0012, 0.0010, 0.0016}, kBlackPaint, 6, 4);
        add_ellipsoid(lamps, head, V3{0.0058, side * 0.0036, 0.0027}, V3{0.00045, 0.00045, 0.00045}, Col{1, 1, 1, 1}, 4, 3);
    }
    add_mesh(paint, hemisphere_mesh(10, 4), head * M34::translate(-0.0004, 0, 0.0040) * M34::scale(0.0066, 0.0064, 0.0050), kDuckHat);
    add_mesh(paint, disc_mesh(10), head * M34::translate(0.0012, 0, 0.0042) * M34::scale(0.0080, 0.0072, 1), kDuckHat);
    add_mesh(paint, disc_mesh(10), head * M34::translate(0.0012, 0, 0.0041) * M34::rot_x(kPi) * M34::scale(0.0080, 0.0072, 1), kDuckHat);
}

// ---------------------------------------------------------------- the house

// The slewing house in its own frame (the slewing centre at the origin, x
// toward the boom's head): turntable, the house with louvres, a door and two
// crank handles, the winch drum on top, stacks, rails, the counterweight
// and the brackets the boom's foot pins into.
void CraneModel::add_house(const M34& turret, const CranePose& pose, double hoist_angle, double luff_angle) {
    (void)pose;
    add_rounded_box(paint, turret, V3{-0.196, -0.071, 0.099}, V3{0.078, 0.071, 0.111}, 0.003, 1, kPaintDeep);
    add_rounded_box(paint, turret, V3{-0.150, -0.068, 0.110}, V3{-0.012, 0.068, 0.188}, 0.008, 2, kPaint);
    // louvres both sides
    for (int side = -1; side <= 1; side += 2) {
        const double y0 = side > 0 ? 0.0672 : -0.0695;
        const double y1 = side > 0 ? 0.0695 : -0.0672;
        for (int l = 0; l < 5; l += 1) {
            const double z = 0.140 + 0.0072 * l;
            add_box(matte, turret, V3{-0.138, y0, z}, V3{-0.094, y1, z + 0.0032}, kBlackPaint);
        }
    }
    // a door on the left with a chrome handle
    {
        const double y0 = 0.0681;
        const double y1 = 0.0689;
        add_box(matte, turret, V3{-0.074, y0, 0.118}, V3{-0.072, y1, 0.178}, kSteel);
        add_box(matte, turret, V3{-0.034, y0, 0.118}, V3{-0.032, y1, 0.178}, kSteel);
        add_box(matte, turret, V3{-0.074, y0, 0.177}, V3{-0.032, y1, 0.179}, kSteel);
        add_box(paint, turret, V3{-0.041, 0.0684, 0.146}, V3{-0.036, 0.0700, 0.149}, kChrome);
    }
    // the crank handles on the right: the hoist and the boom
    const double crank_x[2] = {-0.075, -0.040};
    const double crank_z[2] = {0.130, 0.142};
    const double crank_a[2] = {hoist_angle, luff_angle};
    for (int k = 0; k < 2; k += 1) {
        const M34 boss = turret * M34::translate(crank_x[k], -0.0679, crank_z[k]) * M34::rot_x(kPi / 2);
        const double br[4] = {0.0085, 0.0085, 0.0060, 0.0};
        const double bz[4] = {0.0, 0.0020, 0.0030, 0.0035};
        lathe_in(paint, boss, br, bz, 4, 12, kChrome);
        const M34 arm = boss * M34::rot_z(crank_a[k]);
        add_rounded_box(paint, arm, V3{-0.0018, -0.0018, 0.0028}, V3{0.0145, 0.0018, 0.0046}, 0.0008, 1, kChrome);
        const double kr[3] = {0.0022, 0.0022, 0.0};
        const double kz[3] = {0.0046, 0.0140, 0.0140};
        lathe_in(paint, arm * M34::translate(0.0128, 0, 0), kr, kz, 3, 8, kBlackPaint);
    }
    // the counterweight, black, striped on the back and sides
    add_rounded_box(paint, turret, V3{-0.193, -0.070, 0.103}, V3{-0.148, 0.070, 0.170}, 0.006, 2, kBlackPaint);
    stripe_panel(striped, turret, V3{-0.1946, 0.060, 0.112}, V3{0, -0.120, 0}, V3{0, 0, 0.050}, V3{-1, 0, 0});
    stripe_panel(striped, turret, V3{-0.188, 0.0716, 0.112}, V3{0.034, 0, 0}, V3{0, 0, 0.050}, V3{0, 1, 0});
    stripe_panel(striped, turret, V3{-0.154, -0.0716, 0.112}, V3{-0.034, 0, 0}, V3{0, 0, 0.050}, V3{0, -1, 0});
    // the winch on the roof: a drum wound with cable between yellow cheeks
    {
        const M34 drum = turret * M34::translate(-0.105, 0, 0.206) * M34::rot_x(-kPi / 2);
        const double dr[2] = {0.0118, 0.0118};
        const double dz[2] = {-0.026, 0.026};
        lathe_in(matte, drum, dr, dz, 2, 14, kSteel);
        for (int w = 0; w < 7; w += 1) {
            add_arc_tube(matte, drum * M34::translate(0, 0, -0.0225 + 0.0075 * w), 0.0128, 0.0013, 0, 2 * kPi, 12, 3, kCable);
        }
        for (int end = -1; end <= 1; end += 2) {
            const double fr[4] = {0.0, 0.019, 0.019, 0.0};
            const double fz[4] = {end * 0.0265 - 0.001, end * 0.0265 - 0.001, end * 0.0265 + 0.001, end * 0.0265 + 0.001};
            lathe_in(paint, drum, fr, fz, 4, 16, kPaintDeep);
            // a bolt on the flange, turning with the drum
            add_ellipsoid(paint, drum, V3{0.013 * std::cos(hoist_angle), 0.013 * std::sin(hoist_angle), end * 0.0285}, V3{0.0018, 0.0018, 0.0012}, kChrome, 6, 4);
        }
        for (int side = -1; side <= 1; side += 2) {
            const double y0 = side > 0 ? 0.0295 : -0.0350;
            const double y1 = side > 0 ? 0.0350 : -0.0295;
            add_rounded_box(paint, turret, V3{-0.124, y0, 0.186}, V3{-0.086, y1, 0.222}, 0.004, 1, kPaint);
        }
    }
    // two stacks with rain caps, rattling while the motor runs
    for (int s = 0; s < 2; s += 1) {
        const double y = -0.052 + 0.014 * s;
        const M34 stack = turret * M34::translate(-0.140, y, 0);
        const double sr[2] = {0.0042, 0.0042};
        const double sz[2] = {0.186, 0.232};
        lathe_in(paint, stack, sr, sz, 2, 8, kBlackPaint);
        const double cr[2] = {0.0048, 0.0048};
        const double cz[2] = {0.224, 0.228};
        lathe_in(paint, stack, cr, cz, 2, 8, kChrome);
        const double flap = pose.moving ? 0.35 * std::abs(std::sin(pose.time * 23.0 + s)) : 0.05;
        add_mesh(paint, disc_mesh(8), turret * M34::translate(-0.1442, y, 0.2322) * M34::rot_y(-flap) * M34::translate(0.0042, 0, 0) * M34::scale(0.0050, 0.0050, 1), kBlackPaint);
    }
    // hand rails along the roof
    for (int side = -1; side <= 1; side += 2) {
        const double y = side * 0.062;
        add_rod(paint, at(turret, -0.145, y, 0.200), at(turret, -0.030, y, 0.200), 0.0010, kChrome, 4);
        const double posts[3] = {-0.143, -0.087, -0.032};
        for (int p = 0; p < 3; p += 1) {
            add_rod(paint, at(turret, posts[p], y, 0.187), at(turret, posts[p], y, 0.2008), 0.0010, kChrome, 4);
        }
    }
    // the boom's foot brackets and pin, the ram's bracket
    for (int side = -1; side <= 1; side += 2) {
        const double y0 = side > 0 ? 0.0215 : -0.0275;
        const double y1 = side > 0 ? 0.0275 : -0.0215;
        add_rounded_box(paint, turret, V3{-0.022, y0, 0.106}, V3{0.030, y1, 0.176}, 0.003, 1, kPaintDeep);
    }
    add_rounded_box(paint, turret, V3{0.050, -0.011, 0.108}, V3{0.074, 0.011, 0.124}, 0.002, 1, kPaintDeep);
    // a fire extinguisher strapped on by the door
    {
        const M34 bottle = turret * M34::translate(-0.004, -0.060, 0.111);
        const double er[6] = {0.0, 0.0055, 0.0058, 0.0058, 0.0040, 0.0};
        const double ez[6] = {0.0, 0.0, 0.002, 0.024, 0.029, 0.030};
        lathe_in(paint, bottle, er, ez, 6, 10, Col{0.86f, 0.10f, 0.08f, 1});
        add_rod(paint, at(bottle, 0, 0, 0.029), at(bottle, 0, 0, 0.034), 0.0016, kBlackPaint, 4);
        add_rod(paint, at(bottle, 0, 0, 0.033), at(bottle, 0.006, 0, 0.033), 0.0009, kChrome, 3);
        add_arc_tube(matte, bottle * M34::translate(0, 0, 0.012), 0.0060, 0.0007, 0, 2 * kPi, 10, 3, kBlackPaint);
    }
}

// The control cab on the house's left.
void CraneModel::add_operator(const M34& turret, const CranePose& pose) {
    const double t = pose.time;
    add_rounded_box(paint, turret, V3{-0.004, 0.027, 0.110}, V3{0.068, 0.073, 0.141}, 0.004, 2, kPaint);
    const double px[2][2] = {{0.0615, 0.0680}, {-0.0040, 0.0025}};
    for (int p = 0; p < 2; p += 1) {
        add_box(paint, turret, V3{px[p][0], 0.027, 0.140}, V3{px[p][1], 0.033, 0.191}, kPaint);
        add_box(paint, turret, V3{px[p][0], 0.067, 0.140}, V3{px[p][1], 0.073, 0.191}, kPaint);
    }
    add_rounded_box(paint, turret, V3{-0.007, 0.024, 0.189}, V3{0.071, 0.076, 0.199}, 0.004, 2, kPaint);
    pane(glass, turret, V3{0.0655, 0.033, 0.141}, V3{0, 0.034, 0}, V3{0, 0, 0.048}, V3{1, 0, 0});
    pane(glass, turret, V3{0.0025, 0.0705, 0.141}, V3{0.059, 0, 0}, V3{0, 0, 0.048}, V3{0, 1, 0});
    pane(glass, turret, V3{0.0025, 0.0295, 0.141}, V3{0.059, 0, 0}, V3{0, 0, 0.048}, V3{0, -1, 0});
    pane(glass, turret, V3{-0.0015, 0.033, 0.141}, V3{0, 0.034, 0}, V3{0, 0, 0.048}, V3{-1, 0, 0});
    add_rod(matte, at(turret, 0.0662, 0.036, 0.1425), at(turret, 0.0662, 0.052, 0.1650), 0.0007, kBlackPaint, 3);
    // a work lamp on the cab roof, aimed along the boom
    {
        const M34 lamp = turret * M34::translate(0.060, 0.050, 0.203) * M34::rot_y(kPi / 2 + 0.25);
        const double lr[4] = {0.0, 0.0050, 0.0050, 0.0042};
        const double lz[4] = {-0.006, -0.006, 0.002, 0.0025};
        lathe_in(paint, lamp, lr, lz, 4, 10, kBlackPaint);
        const double gr[2] = {0.0042, 0.0};
        const double gz[2] = {0.0025, 0.0030};
        lathe_in(lamps, lamp, gr, gz, 2, 10, Col{1.0f, 0.97f, 0.85f, 1});
        add_rod(paint, at(turret, 0.060, 0.050, 0.199), at(turret, 0.060, 0.050, 0.205), 0.0012, kChrome, 4);
    }
    // seat and the control desk
    add_rounded_box(matte, turret, V3{0.002, 0.036, 0.141}, V3{0.024, 0.064, 0.146}, 0.002, 1, kSeat);
    add_rounded_box(matte, turret, V3{-0.002, 0.036, 0.143}, V3{0.006, 0.064, 0.172}, 0.002, 1, kSeat);
    add_rounded_box(matte, turret, V3{0.054, 0.034, 0.141}, V3{0.064, 0.066, 0.150}, 0.002, 1, kInterior);
    add_rounded_box(lamps, turret, V3{0.0560, 0.040, 0.1500}, V3{0.0580, 0.044, 0.1508}, 0.0003, 1, Col{0.40f, 0.95f, 0.50f, 1});
    add_rounded_box(lamps, turret, V3{0.0560, 0.056, 0.1500}, V3{0.0580, 0.060, 0.1508}, 0.0003, 1,
                    pose.moving ? Col{1.0f, 0.55f, 0.15f, 1} : Col{0.45f, 0.25f, 0.10f, 1});
    // two levers, rocking while the crane moves
    V3 knob[2];
    for (int k = 0; k < 2; k += 1) {
        const double y = 0.050 + (k == 0 ? -0.013 : 0.013);
        const double tilt = pose.moving ? 0.45 * std::sin(t * 2.3 + 1.7 * k) : 0.0;
        const V3 foot = at(turret, 0.050, y, 0.141);
        knob[k] = at(turret, 0.050 - 0.012 * std::sin(tilt) - 0.002, y, 0.141 + 0.012 * std::cos(tilt));
        add_rod(paint, foot, knob[k], 0.0010, kChrome, 4);
        add_ellipsoid(paint, M34(), knob[k], V3{0.0022, 0.0022, 0.0022}, k == 0 ? kHookRed : kBlackPaint, 8, 5);
    }

}

// ---------------------------------------------------------------- the hook

// The hook block: two striped cheeks round a sheave, a chrome swivel, and a
// red hook with its safety latch; the slings ride in the hook's bowl at `hook`.
void CraneModel::add_hook_block(V3 hook, double slew, V3 line_top, double hook_turn) {
    const M34 block = M34::translate(hook.x, hook.y, hook.z) * M34::rot_z(slew);
    // below the swivel the hook turns freely, with whatever hangs on it
    const M34 below = M34::translate(hook.x, hook.y, hook.z) * M34::rot_z(hook_turn);
    // cheeks
    for (int side = -1; side <= 1; side += 2) {
        const double y0 = side > 0 ? 0.0052 : -0.0080;
        const double y1 = side > 0 ? 0.0080 : -0.0052;
        add_rounded_box(paint, block, V3{-0.0125, y0, 0.023}, V3{0.0125, y1, 0.051}, 0.0035, 1, kPaint);
        stripe_panel(striped, block, V3{-0.0110, side * 0.0090, 0.025}, V3{0.022, 0, 0}, V3{0, 0, 0.024}, V3{0, static_cast<double>(side), 0});
    }
    // the sheave between them, and the pins
    add_can(matte, block * M34::translate(0, 0, 0.040) * M34::rot_x(kPi / 2), 0.0098, -0.0050, 0.0050, 14, kSteel);
    add_rod(paint, at(block, 0, -0.0095, 0.040), at(block, 0, 0.0095, 0.040), 0.0020, kChrome, 6);
    add_rod(paint, at(block, 0, -0.0095, 0.027), at(block, 0, 0.0095, 0.027), 0.0016, kChrome, 6);
    // the swivel
    add_can(paint, block, 0.0030, 0.0150, 0.0235, 10, kChrome);
    // the hook: a C round the bowl, opening at the front, and its latch
    const double bowl = 0.0075;
    const M34 curve = below * M34::translate(0, 0, bowl) * M34::rot_x(kPi / 2);
    const double a0 = 0.62 * kPi;
    const double a1 = 2.12 * kPi;
    add_arc_tube(paint, curve, bowl, 0.0024, a0, a1, 14, 6, kHookRed);
    const V3 shank_foot = at(curve, bowl * std::cos(a0), bowl * std::sin(a0), 0);
    add_rod(paint, shank_foot, at(below, 0, 0, 0.0155), 0.0024, kHookRed, 6);
    const V3 tip = at(curve, bowl * std::cos(a1), bowl * std::sin(a1), 0);
    add_ellipsoid(paint, M34(), tip, V3{0.0026, 0.0026, 0.0026}, kHookRed, 6, 4);
    add_rod(paint, at(below, -0.0012, 0, 0.0150), tip, 0.0006, kChrome, 3);
    // the line from the head sheave down to the block
    add_rod(matte, line_top, at(block, 0, 0, 0.049), 0.0011, kCable, 4);
}

// ---------------------------------------------------------------- per frame

void CraneModel::build(const CranePose& pose) {
    paint = truck_paint_;
    matte = truck_matte_;
    striped = truck_striped_;
    glass = truck_glass_;
    lamps = truck_lamps_;
    plates.clear();
    const M34& c = setup_.chassis;
    add_duck(pose);
    // the beacon on the cab roof: turning amber while the motors run
    {
        const M34 beacon = c * M34::translate(0.112, 0, 0.180);
        add_can(paint, beacon, 0.0064, 0.0, 0.0032, 10, kBlackPaint);
        const double phase = pose.time * 7.0;
        const double flash = pose.moving ? 0.30 + 0.70 * std::pow(std::max(0.0, std::cos(phase)), 3.0) : 0.30;
        const Col amber{static_cast<float>(0.35 + 0.65 * flash), static_cast<float>(0.18 + 0.42 * flash), static_cast<float>(0.04 + 0.08 * flash), 1};
        const double dr[4] = {0.0055, 0.0055, 0.0040, 0.0};
        const double dz[4] = {0.0032, 0.0090, 0.0118, 0.0124};
        lathe_in(lamps, beacon, dr, dz, 4, 10, amber);
        beacon_at = at(beacon, 0, 0, 0.008);
        beacon_glow = pose.moving ? std::pow(std::max(0.0, std::cos(phase)), 3.0) : 0.0;
    }
    // a whip aerial on the cab's back corner, an orange pennant fluttering on it
    {
        const V3 foot = at(c, 0.100, 0.066, 0.180);
        const double lean = 0.04 * std::sin(pose.time * 1.9);
        const V3 tip = foot + V3{lean * 0.10, 0.004, 0.110};
        add_rod(paint, foot, tip, 0.0007, kBlackPaint, 3);
        const double flutter = std::sin(pose.time * 6.0);
        const V3 down = (foot - tip) * (0.024 / len(foot - tip));
        const V3 fly{0.016 * std::cos(0.8 + 0.3 * flutter), 0.016 * std::sin(0.8 + 0.3 * flutter), -0.002};
        const double st[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        const V3 a = tip;
        const V3 b = tip + down;
        const V3 f = tip + down * 0.5 + fly;
        add_quad(matte, a, b, f, f, st, cross(b - a, f - a), Col{1.0f, 0.42f, 0.08f, 1});
    }
    // the house turns to face the hook
    const V3 hook = pose.hook;
    const V3 pivot = at(c, -0.06, 0, 0.165);
    const double relative = std::remainder(std::atan2(hook.y - pivot.y, hook.x - pivot.x) - setup_.heading, 2 * kPi);
    // A physical stop in chassis coordinates also covers its small terrain
    // tilt and any externally restored hook position.
    const double limit = site_layout().crane_slew_limit;
    const double slew = setup_.heading + std::clamp(relative, -limit, limit);
    const M34 turret = c * M34::translate(-0.06, 0, 0) * M34::rot_z(slew - setup_.heading);
    const V3 root = at(turret, 0.012, 0, 0.165);
    const V3 forward{std::cos(slew), std::sin(slew), 0};
    // The boom stands at a working angle and telescopes out to the reach; the
    // hook goes up and down on the line paid out over the head sheave, so the
    // boom never sinks toward the cab. Only a hook lifted high takes the head
    // higher still, to keep some line between them.
    const V3 over_hook = hook - forward * 0.0135;
    const double flat_reach = std::sqrt((over_hook.x - root.x) * (over_hook.x - root.x) + (over_hook.y - root.y) * (over_hook.y - root.y));
    const double working_angle = 0.52;   // about 30 degrees
    const double head_height = std::max(root.z + std::tan(working_angle) * flat_reach, hook.z + 0.049 + 0.07);
    const V3 line_point{hook.x, hook.y, head_height};
    V3 head = line_point - forward * 0.0135;
    V3 along = head - root;
    double boom = len(along);
    const double shortest = 0.36;
    if (boom < shortest) {
        const double flat = std::sqrt(along.x * along.x + along.y * along.y);
        head.z = root.z + std::sqrt(std::max(0.0, shortest * shortest - flat * flat));
        along = head - root;
        boom = shortest;
    }
    const V3 direction = norm(along);
    const double elevation = std::asin(std::max(-1.0, std::min(1.0, direction.z)));
    const double line_length = head.z - (hook.z + 0.049);
    add_house(turret, pose, line_length * 140.0, elevation * 9.0);
    add_operator(turret, pose);
    // the boom's frame: z along it, x to its left, y up off its back
    const M34 frame = frame_along(root, direction);
    const double piece = boom / 3 + 0.04;
    // the foot section: black pressed-steel channels punched with holes
    {
        const double hw = 0.020;
        const double hh = 0.023;
        const double end = piece;
        add_box(paint, frame, V3{-hw - 0.0015, hh - 0.003, 0.010}, V3{hw + 0.0015, hh, end}, kBlackPaint);
        add_box(paint, frame, V3{-hw - 0.0015, -hh, 0.010}, V3{hw + 0.0015, -hh + 0.003, end}, kBlackPaint);
        const double spacing = 2 * hh - 0.006;
        const double s1 = (end - 0.010) / spacing;
        const double st[4][2] = {{0, 0}, {s1, 0}, {s1, 1}, {0, 1}};
        for (int side = -1; side <= 1; side += 2) {
            const double x = side * hw;
            quad_in(plates, frame, V3{x, -hh + 0.003, 0.010}, V3{x, -hh + 0.003, end}, V3{x, hh - 0.003, end}, V3{x, hh - 0.003, 0.010}, st,
                    V3{static_cast<double>(side), 0, 0}, Col{1, 1, 1, 1});
        }
        // the cast foot round the pin, and the pin through the brackets
        add_rounded_box(paint, frame, V3{-hw - 0.002, -hh - 0.002, -0.014}, V3{hw + 0.002, hh + 0.002, 0.014}, 0.006, 2, kPaintDeep);
        add_rod(paint, at(frame, -0.031, 0, 0), at(frame, 0.031, 0, 0), 0.0036, kChrome, 8);
        // a collar round its mouth
        add_box(paint, frame, V3{-hw - 0.003, hh, end - 0.010}, V3{hw + 0.003, hh + 0.003, end}, kPaint);
        add_box(paint, frame, V3{-hw - 0.003, -hh - 0.003, end - 0.010}, V3{hw + 0.003, -hh, end}, kPaint);
        add_box(paint, frame, V3{hw, -hh - 0.003, end - 0.010}, V3{hw + 0.003, hh + 0.003, end}, kPaint);
        add_box(paint, frame, V3{-hw - 0.003, -hh - 0.003, end - 0.010}, V3{-hw, hh + 0.003, end}, kPaint);
    }
    // the middle and fly sections, yellow, each with a collar and wear pads
    const double start_mid = (boom - piece) * 0.5;
    const double start_fly = boom - piece;
    add_rounded_box(paint, frame, V3{-0.0155, -0.0185, start_mid}, V3{0.0155, 0.0185, start_mid + piece}, 0.003, 1, kPaint);
    add_rounded_box(paint, frame, V3{-0.0170, -0.0200, start_mid + piece - 0.008}, V3{0.0170, 0.0200, start_mid + piece}, 0.002, 1, kPaintDeep);
    add_rounded_box(paint, frame, V3{-0.0115, -0.0140, start_fly}, V3{0.0115, 0.0140, boom - 0.004}, 0.003, 1, kPaintLight);
    for (int pad = 0; pad < 2; pad += 1) {
        const double z = start_mid + piece - 0.006;
        const double x = pad == 0 ? -0.008 : 0.004;
        add_box(matte, frame, V3{x, 0.0200, z - 0.004}, V3{x + 0.004, 0.0212, z}, kBlackPaint);
    }
    // the head: cheeks round the sheave, a red light, a little wind gauge
    const V3 sheave_centre = line_point - forward * 0.0135 + V3{0, 0, -0.0005};
    {
        add_rounded_box(paint, frame, V3{-0.0145, -0.0160, boom - 0.016}, V3{-0.0110, 0.0120, boom + 0.006}, 0.0015, 1, kPaintDeep);
        add_rounded_box(paint, frame, V3{0.0110, -0.0160, boom - 0.016}, V3{0.0145, 0.0120, boom + 0.006}, 0.0015, 1, kPaintDeep);
        const V3 side = norm(cross(V3{0, 0, 1}, forward));
        const M34 sheave = frame_along(sheave_centre - side * 0.0045, side);
        const double sr[6] = {0.0, 0.0130, 0.0122, 0.0122, 0.0130, 0.0};
        const double sz[6] = {0.0, 0.0, 0.003, 0.006, 0.009, 0.009};
        lathe_in(matte, sheave, sr, sz, 6, 16, kSteel);
        add_rod(paint, sheave_centre - side * 0.016, sheave_centre + side * 0.016, 0.0022, kChrome, 6);
        const double turn = line_length / 0.0125;
        for (int b = 0; b < 2; b += 1) {
            const double a = turn + kPi * b;
            const V3 spot = sheave_centre + side * (0.0095 * (b == 0 ? 1 : -1)) + forward * (0.0065 * std::cos(a)) + V3{0, 0, 0.0065 * std::sin(a)};
            add_ellipsoid(paint, M34(), spot, V3{0.0012, 0.0012, 0.0012}, kChrome, 5, 3);
        }
        // red and white bands round the head, so it's seen against the sky
        for (int band = 0; band < 3; band += 1) {
            const double z0 = boom - 0.016 + 0.0075 * band;
            const Col tone = band % 2 == 0 ? Col{0.86f, 0.12f, 0.10f, 1} : Col{0.96f, 0.96f, 0.94f, 1};
            for (int side = -1; side <= 1; side += 2) {
                const double x = side * 0.01465;
                const double st[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
                quad_in(paint, frame, V3{x, -0.0155, z0}, V3{x, -0.0155, z0 + 0.0075}, V3{x, 0.0115, z0 + 0.0075}, V3{x, 0.0115, z0}, st,
                        V3{static_cast<double>(side), 0, 0}, tone);
            }
        }
        const V3 crown = at(frame, 0, 0.016, boom - 0.004);
        add_ellipsoid(lamps, M34(), crown, V3{0.0026, 0.0026, 0.0026}, Col{0.95f, 0.15f, 0.10f, 1}, 8, 5);
        const V3 mast_top = crown + V3{0, 0, 0.014};
        add_rod(paint, crown, mast_top, 0.0008, kChrome, 4);
        for (int cup = 0; cup < 3; cup += 1) {
            const double a = pose.time * 5.0 + 2 * kPi * cup / 3;
            const V3 tip{mast_top.x + 0.0055 * std::cos(a), mast_top.y + 0.0055 * std::sin(a), mast_top.z};
            add_rod(paint, mast_top, tip, 0.0005, kChrome, 3);
            add_ellipsoid(paint, M34(), tip, V3{0.0016, 0.0016, 0.0016}, kBlackPaint, 6, 4);
        }
    }
    // the luffing ram: a yellow barrel off the house, a chrome rod up to the boom
    {
        const V3 base = at(turret, 0.064, 0, 0.118);
        const V3 lug = at(frame, 0, -0.026, 0.33 * piece);
        add_rounded_box(paint, frame, V3{-0.008, -0.0265, 0.33 * piece - 0.010}, V3{0.008, -0.022, 0.33 * piece + 0.010}, 0.002, 1, kPaintDeep);
        const V3 run = lug - base;
        const double run_length = len(run);
        const V3 unit = norm(run);
        const double barrel = std::min(0.080, run_length - 0.012);
        add_rod(paint, base, base + unit * barrel, 0.0078, kPaintDeep, 10);
        add_mesh(paint, disc_mesh(10), frame_along(base + unit * barrel, unit) * M34::scale(0.0078, 0.0078, 1), kPaintDeep);
        add_rod(paint, base + unit * (barrel - 0.004), lug, 0.0048, kChrome, 8);
        add_ellipsoid(paint, M34(), base, V3{0.0060, 0.0060, 0.0060}, kChrome, 8, 5);
        add_ellipsoid(paint, M34(), lug, V3{0.0050, 0.0050, 0.0050}, kChrome, 8, 5);
        // its two hoses, drooping from the house
        for (int h = 0; h < 2; h += 1) {
            const V3 from = at(turret, 0.050, -0.006 + 0.012 * h, 0.124);
            const V3 to = base + unit * (barrel - 0.010) + V3{0, 0, 0.007};
            const V3 middle = (from + to) * 0.5 - V3{0, 0, 0.006};
            add_rod(matte, from, middle, 0.0012, kBlackPaint, 4);
            add_rod(matte, middle, to, 0.0012, kBlackPaint, 4);
        }
    }
    // the hoist line: off the drum, over a guide at the boom's foot, along the
    // boom's back to the head sheave
    {
        const V3 drum_top = at(turret, -0.105, 0, 0.206 + 0.0135);
        const V3 guide = at(frame, 0, 0.0265, 0.020);
        const V3 head_top = sheave_centre + V3{0, 0, 0.0128} - forward * 0.002;
        add_rod(matte, drum_top, guide, 0.0011, kCable, 4);
        add_rod(matte, guide, head_top, 0.0011, kCable, 4);
        add_can(paint, frame_along(guide - at(frame, 0.006, 0, 0) + at(frame, 0, 0, 0), at(frame, 1, 0, 0) - at(frame, 0, 0, 0)), 0.0045, 0.0, 0.012, 10, kSteel);
    }
    add_hook_block(hook, slew, line_point, pose.hook_follows ? pose.hook_turn : slew);
}

}  // namespace zc
