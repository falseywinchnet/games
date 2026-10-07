#include "sim.hpp"

#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>

namespace eggy {

namespace {
constexpr double kGravity = 13.0;
constexpr double kHopVel = 4.8;  // apex ≈ 0.89
constexpr double kStepUp = 0.28; // taller than this needs a hop
constexpr double kDuckR = 0.18;
double clamp01(double x) { return std::clamp(x, 0.0, 1.0); }
}  // namespace

Sim::Sim(std::uint64_t seed) : world(seed), rng_(seed ^ 0xD0C4ULL) {
    collected.assign(world.stars().size(), 0);
    d.u = world.lane(6.5);   // at base camp, by the campfire
    d.v = 6.5;
    d.z = world.ground(d.u, d.v);
    best_v = d.v;
    last_biome = world.row(2).biome;
}

double Sim::day_phase() const {
    const double day = 20 * 60.0;
    double p = std::fmod(elapsed / day + day_offset, 1.0);
    return p < 0 ? p + 1 : p;
}
double Sim::sun() const {
    const double p = day_phase();
    return clamp01(std::sin((p - .2) / .6 * M_PI) * 1.4);
}

void Sim::begin(Act a, double len) {
    d.act = a;
    d.act_t = 0;
    d.act_len = len;
}

bool Sim::passable(int u, std::int64_t v) const {
    if (u < 0 || u >= kWidth || v < 0) return false;
    const Tile& t = world.tile(u, v);
    if (World::blocking(t.feature)) return false;
    if (world.slippery(t)) return false;
    return true;
}

double Sim::surface_speed(const Tile& t) const {
    switch (t.surface) {
        case Surface::snow: return .72;
        case Surface::gravel: return .86;
        case Surface::rock: return .9;
        case Surface::water: return (t.feature == Feature::stone || t.feature == Feature::lily) ? .9 : .55;
        case Surface::ice: return .8;
        case Surface::moss: return .95;
        case Surface::path: return 1.05;
        default: return 1;
    }
}

void Sim::place_at(double v) {
    v = std::clamp(v, 2.0, static_cast<double>(world.length()) - 2);
    const std::int64_t iv = static_cast<std::int64_t>(v);
    double best = world.lane(v);
    for (int k = 0; k < kWidth; ++k) {
        const int u = static_cast<int>(world.lane(v)) + ((k & 1) ? -(k + 1) / 2 : k / 2);
        if (u >= 0 && u < kWidth && passable(u, iv) && world.tile(u, iv).surface != Surface::water) { best = u + .5; break; }
    }
    d.u = best;
    d.v = iv + .5;
    d.z = world.ground(d.u, d.v);
    d.vu = d.vv = d.vz = 0;
    d.air = d.sliding = false;
    d.act = Act::none;
    path.clear();
    best_v = std::max(best_v, d.v);
    leaves.clear();
    prints.clear();
}

void Sim::advance_offline(double seconds, double& rows_gained) {
    rows_gained = 0;
    if (finished || seconds <= 0) return;
    elapsed += seconds;
    const double target = std::min(d.v + seconds * kOfflineRowsPerSecond, static_cast<double>(world.length()) - 30);
    rows_gained = std::max(0.0, target - d.v);
    if (rows_gained > 0.5) place_at(target);
    last_milestone = static_cast<int>(world.altitude_m(d.v) / 1000);
    d.breath = 1;
}

void Sim::step(double dt) {
    dt = std::clamp(dt, 0.0, 0.25);
    while (dt > 1e-6) {
        const double h = std::min(dt, 1.0 / 120);
        tick(h);
        dt -= h;
    }
}

// ------------------------------------------------------------------ autopilot
void Sim::plan(bool to_target) {
    path.clear();
    const int su = std::clamp(static_cast<int>(d.u), 0, kWidth - 1);
    const std::int64_t sv = static_cast<std::int64_t>(d.v);
    const std::int64_t lo = sv - 5, hi = sv + 26;
    const int rows = static_cast<int>(hi - lo + 1);
    auto id = [&](int u, std::int64_t v) { return static_cast<int>(v - lo) * kWidth + u; };
    const int N = rows * kWidth;
    std::vector<double> g(static_cast<size_t>(N), 1e18);
    std::vector<int> from(static_cast<size_t>(N), -1);
    using Q = std::pair<double, int>;
    std::priority_queue<Q, std::vector<Q>, std::greater<Q>> open;
    int tu = -1;
    std::int64_t tv = 0;
    if (to_target) {
        tu = std::clamp(static_cast<int>(in.tu), 0, kWidth - 1);
        tv = std::clamp(static_cast<std::int64_t>(in.tv), lo, hi);
    }
    auto heur = [&](int u, std::int64_t v) {
        if (to_target) return std::hypot(static_cast<double>(u - tu), static_cast<double>(v - tv));
        return static_cast<double>(hi - v) * .9;
    };
    const int start = id(su, sv);
    g[static_cast<size_t>(start)] = 0;
    open.push({heur(su, sv), start});
    int best = start;
    double best_score = -1e18;
    while (!open.empty()) {
        auto [f, cur] = open.top();
        open.pop();
        const int cu = cur % kWidth;
        const std::int64_t cv = lo + cur / kWidth;
        const double gc = g[static_cast<size_t>(cur)];
        if (f > gc + heur(cu, cv) + 1e-9) continue;
        if (to_target) {
            if (cu == tu && cv == tv) { best = cur; break; }
            const double sc = -heur(cu, cv) * 3 - gc * .1;
            if (sc > best_score) { best_score = sc; best = cur; }
        } else {
            const double sc = static_cast<double>(cv) * 10 - gc;
            if (sc > best_score) { best_score = sc; best = cur; }
            if (cv >= hi - 1) { best = cur; break; }
        }
        for (int du = -1; du <= 1; ++du)
            for (int dv = -1; dv <= 1; ++dv) {
                if (!du && !dv) continue;
                const int nu = cu + du;
                const std::int64_t nv = cv + dv;
                if (nv < lo || nv > hi || nu < 0 || nu >= kWidth) continue;
                if (!passable(nu, nv)) continue;
                if (du && dv && (!passable(cu + du, cv) || !passable(cu, cv + dv))) continue;
                const Tile& t = world.tile(nu, nv);
                double cost = (du && dv ? 1.414 : 1.0) / surface_speed(t);
                if (dv > 0) {
                    const double rise = world.row(nv).riser;
                    if (rise > kMaxHop - .05) continue;
                    if (rise > kStepUp) cost += .45;
                } else if (dv < 0) cost += .9;
                if (World::hop_height(t.feature) > 0) cost += .5;
                if (t.surface == Surface::water && t.feature != Feature::stone && t.feature != Feature::lily) cost += 1.2;
                const int nid = id(nu, nv);
                const double ng = gc + cost;
                if (ng < g[static_cast<size_t>(nid)]) {
                    g[static_cast<size_t>(nid)] = ng;
                    from[static_cast<size_t>(nid)] = cur;
                    open.push({ng + heur(nu, nv), nid});
                }
            }
    }
    std::vector<std::pair<double, double>> rev;
    for (int c = best; c != -1 && c != start; c = from[static_cast<size_t>(c)])
        rev.push_back({c % kWidth + .5, static_cast<double>(lo + c / kWidth) + .5});
    path.assign(rev.rbegin(), rev.rend());
    if (to_target && !path.empty()) path.back() = {in.tu, in.tv};
}

// ------------------------------------------------------------------ movement
bool Sim::try_hop() {
    if (d.air || d.act != Act::none) return false;
    d.air = true;
    d.vz = kHopVel * (d.swim ? .8 : 1.0);
    d.breath = std::max(0.0, d.breath - .006);
    emit(Ev::hop);
    return true;
}

void Sim::move(double dt, double du, double dv, double speed) {
    const Tile& here = world.tile(std::clamp(static_cast<int>(d.u), 0, kWidth - 1), static_cast<std::int64_t>(d.v));
    const bool on_ice = !d.air && world.slippery(here);
    d.swim = !d.air && here.surface == Surface::water && here.feature != Feature::stone && here.feature != Feature::lily;
    if (d.swim) last_swim_ = elapsed;
    const double want_u = du * speed, want_v = dv * speed;
    double traction = d.air ? 2.5 : 11;
    if (on_ice) traction = 1.1;
    if (d.swim) traction = 4;
    d.vu += (want_u - d.vu) * std::min(1.0, dt * traction);
    d.vv += (want_v - d.vv) * std::min(1.0, dt * traction);
    if (on_ice) {
        d.vv -= 2.6 * dt;  // gravity drags you back down the ice
        d.vv = std::min(d.vv, .25);
    }
    // the wind
    if (gust_t < gust_len) {
        const double s = gust_strength * std::sin(M_PI * gust_t / gust_len);
        d.vu += gust_du * s * 2.2 * dt;
        d.vv += gust_dv * s * 2.2 * dt;
    }
    const bool was_sliding = d.sliding;
    d.sliding = on_ice && d.vv < -.35;
    if (d.sliding && !was_sliding) emit(Ev::slide);

    double nu = d.u + d.vu * dt, nv = d.v + d.vv * dt;
    nu = std::clamp(nu, .2, kWidth - .2);
    if (nu == .2 || nu == kWidth - .2) d.vu = 0;
    nv = std::max(nv, .3);
    // obstacles around the destination
    const int cu = static_cast<int>(nu);
    const std::int64_t cv = static_cast<std::int64_t>(nv);
    bool blocked_hop = false;
    for (int ou = -1; ou <= 1; ++ou)
        for (int ov = -1; ov <= 1; ++ov) {
            const int tu = cu + ou;
            const std::int64_t tv = cv + ov;
            if (tu < 0 || tu >= kWidth || tv < 0) continue;
            const Tile& t = world.tile(tu, tv);
            const double above = d.z - world.ground(std::clamp(nu, 0.0, kWidth - .01), nv);
            if (t.feature == Feature::log) {
                if (above > .4) continue;
                const double lo = tv + .32, hi = tv + .68;
                if (nu >= tu - .05 && nu <= tu + 1.05 && nv > lo - kDuckR && nv < hi + kDuckR) {
                    blocked_hop = true;
                    nv = d.v <= lo ? std::min(nv, lo - kDuckR) : std::max(nv, hi + kDuckR);
                    d.vv = 0;
                }
                continue;
            }
            double r = 0;
            if (World::blocking(t.feature)) {
                r = t.feature == Feature::boulder ? .4 : t.feature == Feature::stump ? .3 : t.feature == Feature::signpost ? .16 : .34;
            } else if (World::hop_height(t.feature) > 0 && above < World::hop_height(t.feature) - .05) {
                r = t.feature == Feature::snowdrift ? .3 : .27;
                blocked_hop = true;
            }
            if (r <= 0) continue;
            const double cx = tu + t.fx, cy = tv + t.fy;
            const double ddx = nu - cx, ddy = nv - cy, dist = std::hypot(ddx, ddy);
            const double need = r + kDuckR;
            if (dist < need && dist > 1e-6) {
                nu = cx + ddx / dist * need;
                nv = cy + ddy / dist * need;
                const double nx = ddx / dist, ny = ddy / dist;
                const double into = d.vu * nx + d.vv * ny;
                if (into < 0) { d.vu -= into * nx; d.vv -= into * ny; }
            } else if (World::hop_height(t.feature) <= 0 || dist >= need) {
                if (World::hop_height(t.feature) > 0) blocked_hop = false || blocked_hop;
            }
        }
    nu = std::clamp(nu, .2, kWidth - .2);
    const double g_new = world.ground(nu, nv);
    if (!d.air) {
        if (g_new - d.z > kStepUp) {  // a riser in the way
            nv = std::floor(nv) - .02;
            if (nv < d.v - .5) nv = d.v;
            d.vv = std::min(0.0, d.vv);
            if (g_new - d.z < kMaxHop + .05) blocked_hop = true;
        }
        const double g2 = world.ground(nu, nv);
        if (g2 < d.z - .35) { d.air = true; d.vz = 0; }
        else d.z = g2;
    } else if (d.z < g_new - .02) {  // flying into a step: slide along it
        nv = d.v;
        d.vv = std::min(0.0, d.vv);
    }
    const double moved = std::hypot(nu - d.u, nv - d.v);
    d.u = nu;
    d.v = nv;
    d.speed = moved / std::max(1e-6, dt);
    if (std::hypot(du, dv) > .1 && !d.sliding) {
        const double target = std::atan2(dv, du);
        double diff = std::remainder(target - d.heading, 2 * M_PI);
        d.heading += diff * std::min(1.0, dt * 9);
    }
    if (blocked_hop && std::hypot(du, dv) > .1 && !d.swim) try_hop();
    if (!d.air && d.speed > .05) {
        d.walk_phase += moved * (d.swim ? 2.2 : 4.6);
        step_dist_ += moved;
        if (step_dist_ > (d.swim ? .5 : .23)) {
            step_dist_ = 0;
            const Tile& t = world.tile(std::clamp(static_cast<int>(d.u), 0, kWidth - 1), static_cast<std::int64_t>(d.v));
            emit(d.swim ? Ev::swim_stroke : Ev::step, static_cast<int>(t.surface));
            if (!d.swim && (t.surface == Surface::snow)) {
                print_side_ ^= 1;
                prints.push_back({d.u + std::cos(d.heading + M_PI / 2) * .06 * (print_side_ ? 1 : -1),
                                  d.v + std::sin(d.heading + M_PI / 2) * .06 * (print_side_ ? 1 : -1), d.z, d.heading, print_side_, 0});
                if (prints.size() > 420) prints.pop_front();
            }
        }
    }
}

// ------------------------------------------------------------------ hazards
void Sim::hazards(double dt) {
    SegmentInfo s = world.segment_at(d.v);
    // storms
    next_storm -= dt;
    if (storm_left > 0) {
        storm_left -= dt;
        if (storm_left <= 0) { storm_target = 0; emit(Ev::storm_end); }
    } else if (next_storm <= 0 && ceremony_t < 0 && d.v > 150) {
        next_storm = rng_.range(1800, 5400);   // every half hour to hour and a half
        storm_left = rng_.range(150, 380);
        storm_target = rng_.range(.75, 1.0);
        emit(Ev::storm_begin);
    }
    storm += (storm_target - storm) * std::min(1.0, dt * .06);
    if (storm > .45) {
        next_flash -= dt;
        if (next_flash <= 0) {
            next_flash = rng_.range(3, 13) / storm;
            const double dist = rng_.uni();
            emit(Ev::lightning, 0, dist);
            thunder_due.push_back({elapsed + .25 + dist * 3.5, dist});
        }
    }
    for (size_t i = 0; i < thunder_due.size();)
        if (elapsed >= thunder_due[i].first) { emit(Ev::thunder, 0, thunder_due[i].second); thunder_due.erase(thunder_due.begin() + static_cast<long>(i)); }
        else ++i;
    s.wind = std::max(s.wind, static_cast<float>(storm * .85));
    // gusts
    if (gust_t < gust_len) gust_t += dt;
    next_gust -= dt;
    if (next_gust <= 0 && ceremony_t < 0) {
        next_gust = rng_.range(6, 22) / (s.wind + .15);
        if (s.wind > .12) {
            gust_t = 0;
            gust_len = rng_.range(1.4, 3.6);
            gust_strength = s.wind * rng_.range(.55, 1.25);
            const double a = -M_PI / 2 + rng_.range(-.9, .9);
            gust_du = std::cos(a);
            gust_dv = std::sin(a);
            if (gust_strength > .3) emit(Ev::gust, 0, gust_strength);
        }
    }
    const double gnow = gust_t < gust_len ? gust_strength * std::sin(M_PI * gust_t / gust_len) : 0;
    wind_vis += (std::max(gnow, s.wind * .25) - wind_vis) * std::min(1.0, dt * 2);
    d.brace += ((gnow > .35 ? 1.0 : 0.0) - d.brace) * std::min(1.0, dt * 5);
    // sun and cold
    const double sunny = sun();
    d.heat += (s.heat * sunny - d.heat) * std::min(1.0, dt * .3);
    d.cold += (s.cold * (1.2 - .5 * sunny) - d.cold) * std::min(1.0, dt * .3);
    // leaves
    if (s.leaves > 0 && ceremony_t < 0) {
        if (rng_.uni() < s.leaves * .7 * dt) {
            Leaf l{};
            const bool aim = rng_.uni() < .035;
            const double fall = rng_.range(5, 7);
            const double t = fall / .85;
            l.u = aim ? d.u + d.vu * t : d.u + rng_.range(-5, 5);
            l.v = aim ? d.v + d.vv * t : d.v + rng_.range(-3, 8);
            l.u = std::clamp(l.u, .2, kWidth - .2);
            l.z = world.ground(l.u, l.v) + fall;
            l.vz = -.85;
            l.phase = rng_.range(0, 6.28);
            l.spin = rng_.range(-3, 3);
            l.color = rng_.below(4) + (s.autumn ? 0 : 4);
            l.aimed = aim;
            if (leaves.size() < 90) leaves.push_back(l);
        }
    }
    for (Leaf& l : leaves) {
        if (l.landed >= 0) { l.landed += dt; continue; }
        l.phase += dt * 2.2;
        l.u += (std::sin(l.phase) * .55 + (gnow * gust_du * .8)) * dt;
        l.v += (std::cos(l.phase * .7) * .25 + (gnow * gust_dv * .8)) * dt;
        l.z += l.vz * dt;
        const double gz = world.ground(std::clamp(l.u, 0.0, kWidth - .01), std::max(0.0, l.v));
        if (l.z <= gz + .02) { l.z = gz + .02; l.landed = 0; }
        const double dist = std::hypot(l.u - d.u, l.v - d.v);
        if (dist < .3 && l.z > d.z + .1 && l.z < d.z + .75 && !d.air && d.act != Act::knocked && ceremony_t < 0) {
            begin(Act::knocked, 2.7);
            d.knock_dir = (l.u < d.u) ? 1 : -1;
            d.vu = d.vv = 0;
            emit(Ev::knocked);
            l.vz = -2.0;
            l.u += .5 * d.knock_dir;
        }
    }
    leaves.erase(std::remove_if(leaves.begin(), leaves.end(), [&](const Leaf& l) {
        return l.landed > 25 || std::fabs(l.v - d.v) > 14 || l.u < -1 || l.u > kWidth + 1;
    }), leaves.end());
    for (Footprint& f : prints) f.age += dt;
    while (!prints.empty() && prints.front().age > 240) prints.pop_front();
}

// ------------------------------------------------------------------ ceremony
void Sim::ceremony(double dt) {
    const double t0 = ceremony_t;
    ceremony_t += dt;
    auto crossed = [&](double t) { return t0 < t && ceremony_t >= t; };
    if (crossed(1.5)) emit(Ev::tent_open);
    if (crossed(3.0)) emit(Ev::general_out);
    if (crossed(7.0)) emit(Ev::salute);
    if (crossed(9.0)) emit(Ev::return_salute);
    if (crossed(11.5)) emit(Ev::medal);
    if (crossed(14.5)) emit(Ev::title);
    if (crossed(24.0)) { finished = true; emit(Ev::ceremony_done); }
    d.act = Act::ceremony;
    d.vu = d.vv = 0;
}

// ------------------------------------------------------------------ tick
void Sim::tick(double dt) {
    if (!finished) elapsed += dt;
    events.reserve(16);
    if (finished) { d.act_t += dt; return; }
    if (ceremony_t >= 0) { ceremony(dt); d.act_t += dt; return; }

    const bool input = std::hypot(in.iu, in.iv) > .05 || in.has_target || in.hop;
    if (input) {
        if (!player_mode && since_input > kAutoAfter) emit(Ev::helped);
        since_input = 0;
        player_mode = true;
    } else {
        since_input += dt;
        if (player_mode && since_input > kAutoAfter) {
            player_mode = false;
            path.clear();
            emit(Ev::auto_on);
        }
    }
    hazards(dt);

    // summit approach
    const double summit_v = static_cast<double>(world.length()) - 8;
    if (d.v >= summit_v - 6 && !d.air) {
        const double tu = world.lane(summit_v) - .9;
        const double ddu = tu - d.u, ddv = summit_v - d.v;
        if (std::hypot(ddu, ddv) < .25) {
            ceremony_t = 0;
            emit(Ev::summit_seen);
            d.heading = M_PI / 2;
            return;
        }
        const double l = std::hypot(ddu, ddv);
        d.act = Act::none;
        move(dt, ddu / l, ddv / l, kAutoSpeed * .8);
        return;
    }

    // activities that hold the duck in place
    if (d.act != Act::none) {
        d.act_t += dt;
        const bool interruptible = d.act == Act::preen || d.act == Act::flap || d.act == Act::look ||
                                   d.act == Act::chirp || d.act == Act::fan || d.act == Act::shiver;
        if (d.act == Act::rest) d.breath = std::min(1.0, d.breath + dt * .19);
        if (d.act == Act::drink && d.act_t >= 1.9 && d.act_t - dt < 1.9) {
            d.breath = 1;
            d.refreshed = 45;
            d.last_drink = elapsed;
            emit(Ev::refreshing);
        }
        if (d.act_t >= d.act_len || (interruptible && input)) {
            if (d.act == Act::rest) emit(Ev::rested);
            if (d.act == Act::knocked) emit(Ev::got_up);
            d.act = Act::none;
        }
        if (d.act != Act::none) {
            move(dt, 0, 0, 0);  // gravity, ice and wind still apply
            if (!d.air && d.vz <= 0) d.vz = 0;
            goto gravity;
        }
    }

    {
        // where does Eggy want to go?
        double du = 0, dv = 0;
        double speed = player_mode ? kPlayerSpeed : kAutoSpeed;
        if (player_mode) {
            if (!in.has_target && std::hypot(in.iu, in.iv) > .05) {
                // keys express intent; Eggy still finds his own way around things
                const double l = std::hypot(in.iu, in.iv);
                in.tu = std::clamp(d.u + in.iu / l * 4.5, .5, kWidth - .5);
                in.tv = std::max(1.0, d.v + in.iv / l * 9);
                keys_target_ = true;
            }
            if (in.has_target || keys_target_) {
                replan_t_ -= dt;
                if (replan_t_ <= 0 || path.empty()) { plan(true); replan_t_ = .35; }
            } else {
                path.clear();
            }
            keys_target_ = false;
            if (in.hop) try_hop();
        } else {
            replan_t_ -= dt;
            if (replan_t_ <= 0 || path.empty()) { plan(false); replan_t_ = 1.6; }
        }
        if (!path.empty()) {
            while (!path.empty() && std::hypot(path.front().first - d.u, path.front().second - d.v) < .3) path.erase(path.begin());
            if (!path.empty()) {
                du = path.front().first - d.u;
                dv = path.front().second - d.v;
            }
        }
        if (hold) { du = dv = 0; path.clear(); }
        const double l = std::hypot(du, dv);
        if (l > 1e-6) { du /= l; dv /= l; }
        const Tile& here = world.tile(std::clamp(static_cast<int>(d.u), 0, kWidth - 1), static_cast<std::int64_t>(d.v));
        speed *= surface_speed(here);
        speed *= 1 - .25 * d.cold;
        if (d.breath < .2) speed *= .78;
        if (d.brace > .5) speed *= .55;
        speed *= 1 - .25 * storm;
        const bool moving = l > 1e-6;
        // breath
        if (moving && !d.air) {
            const double uphill = std::max(0.0, dv);
            double drain = .0105 * (speed / kPlayerSpeed) * (.45 + .55 * uphill) * (1 + d.heat * .9 + d.cold * .35);
            if (d.refreshed > 0) drain *= .5;
            d.breath -= drain * dt;
        } else {
            d.breath = std::min(1.0, d.breath + dt * .03);
        }
        d.refreshed = std::max(0.0, d.refreshed - dt);
        if (d.breath < .22 && !breath_warned_) { breath_warned_ = true; emit(Ev::breath_low); }
        if (d.breath > .5) breath_warned_ = false;
        if (d.breath <= 0 && !d.air) {
            d.breath = 0;
            begin(Act::rest, rng_.range(4.5, 7.5));
            emit(Ev::breathless);
        }
        // thirsty? drink at water or a puddle within reach
        if (d.act == Act::none && !d.air && !d.swim && (d.breath < .6 || elapsed - d.last_drink > 300) && elapsed - d.last_drink > 25) {
            for (int ou = -1; ou <= 1 && d.act == Act::none; ++ou)
                for (int ov = -1; ov <= 1; ++ov) {
                    const int tu = static_cast<int>(d.u) + ou;
                    const std::int64_t tv = static_cast<std::int64_t>(d.v) + ov;
                    if (tu < 0 || tu >= kWidth || tv < 0) continue;
                    const Tile& t = world.tile(tu, tv);
                    const bool water = (t.surface == Surface::water) || t.feature == Feature::puddle;
                    if (water && std::hypot(tu + .5 - d.u, tv + .5 - d.v) < 1.05) {
                        d.heading = std::atan2(tv + .5 - d.v, tu + .5 - d.u);
                        begin(Act::drink, 3.1);
                        d.last_drink = elapsed;
                        emit(Ev::drink);
                        break;
                    }
                }
        }
        // little breaks: preen, flap, look about, chirp
        if (d.act == Act::none && !d.air) {
            if (!moving) {
                d.stand_t += dt;
                if (d.stand_t > d.next_idle) {
                    d.stand_t = 0;
                    d.next_idle = rng_.range(3, 7);
                    const double r = rng_.uni();
                    const Act a = r < .3 ? Act::preen : r < .55 ? Act::flap : r < .75 ? Act::look : r < .88 ? Act::chirp
                                : (d.cold > .5 ? Act::shiver : d.heat > .45 ? Act::fan : Act::look);
                    begin(a, a == Act::preen ? 3.2 : a == Act::flap ? 1.6 : a == Act::look ? 2.4 : a == Act::chirp ? 1.2 : 2.0);
                    emit(a == Act::preen ? Ev::preen : a == Act::flap ? Ev::flap : a == Act::chirp ? Ev::chirp
                         : a == Act::shiver ? Ev::shiver : a == Act::fan ? Ev::fan : Ev::look);
                }
            } else {
                d.stand_t = 0;
                if (!player_mode) {
                    d.next_break -= dt;
                    if (d.next_break <= 0) {
                        d.next_break = rng_.range(35, 95);
                        const double r = rng_.uni();
                        const Act a = d.cold > .55 && r < .35 ? Act::shiver : d.heat > .5 && r < .35 ? Act::fan
                                    : r < .45 ? Act::preen : r < .75 ? Act::flap : Act::look;
                        begin(a, a == Act::preen ? 3.0 : 1.8);
                        emit(a == Act::preen ? Ev::preen : a == Act::flap ? Ev::flap : a == Act::shiver ? Ev::shiver
                             : a == Act::fan ? Ev::fan : Ev::look);
                    }
                }
            }
        }
        if (d.act == Act::none) move(dt, du, dv, moving ? speed : 0);
        else move(dt, 0, 0, 0);
        // stuck watchdog for the autopilot
        if (!player_mode) {
            stuck_t_ += dt;
            if (stuck_t_ > 2.0) {
                if (d.v - last_v_check_ < .15 && d.act == Act::none) {
                    path.clear();
                    replan_t_ = 0;
                    if (rng_.uni() < .5) try_hop();
                    d.vu += rng_.range(-1.5, 1.5);
                }
                stuck_t_ = 0;
                last_v_check_ = d.v;
            }
        }
    }

gravity:
    if (d.air) {
        d.vz -= kGravity * dt;
        d.z += d.vz * dt;
        const double g = world.ground(d.u, d.v);
        if (d.z <= g) {
            d.z = g;
            d.air = false;
            const Tile& t = world.tile(std::clamp(static_cast<int>(d.u), 0, kWidth - 1), static_cast<std::int64_t>(d.v));
            emit(t.surface == Surface::water && t.feature != Feature::stone && t.feature != Feature::lily ? Ev::splash : Ev::land,
                 static_cast<int>(t.surface), -d.vz);
            d.vz = 0;
        }
    }
    // stars: only for the helping hand, never for the autopilot
    if (player_mode) {
        const auto& st = world.stars();
        auto it = std::lower_bound(st.begin(), st.end(), static_cast<std::int64_t>(d.v) - 1,
                                   [](const Star& s, std::int64_t x) { return s.v < x; });
        for (; it != st.end() && it->v <= static_cast<std::int64_t>(d.v) + 1; ++it) {
            const size_t i = static_cast<size_t>(it - st.begin());
            if (collected[i]) continue;
            if (std::hypot(it->u - d.u, (it->v + .5) - d.v) < .5) {
                collected[i] = 1;
                ++stars_collected;
                emit(Ev::star, static_cast<int>(i));
            }
        }
    }
    best_v = std::max(best_v, d.v);
    const int m = static_cast<int>(world.altitude_m(d.v) / 1000);
    if (m > last_milestone) { last_milestone = m; emit(Ev::milestone, m); }
    const Biome b = world.row(static_cast<std::int64_t>(d.v)).biome;
    if (b != last_biome) { last_biome = b; emit(Ev::biome, static_cast<int>(b)); }
}

}  // namespace eggy
