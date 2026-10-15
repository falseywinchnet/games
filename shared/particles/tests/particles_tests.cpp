// The particle engine's own tests: that births follow their rate and box, that the
// same calls give the same particles whatever the steps they are made in, that free
// fall and the floor behave, that a full pool skips births and says so, and that
// drawing stays inside the picture, respects what hides it, and composes as it says.
#include "particles.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

particles::Emitter fountain() {
    particles::Emitter e;
    e.low = {-1, 10, 5};
    e.high = {1, 10, 5};
    e.rate = 600;
    e.velocity = {0, 0, 0};
    e.velocity_spread = {.5f, .5f, .5f};
    e.life = {3, 4};
    e.size = {.05f, .1f};
    e.tail = {.02f, .05f};
    e.seed = 7;
    return e;
}

void births_follow_rate_and_box() {
    particles::System system(10000);
    static_cast<void>(system.add_emitter(fountain()));
    system.advance(1.0);
    check(system.count() >= 595 && system.count() <= 605, "a second at 600 a second makes about 600");
    bool inside = true;
    for (const particles::Vec& p : system.positions())
        if (p.x < -2 || p.x > 2 || p.z < 4 || p.z > 6 || p.y > 10.1f)
            inside = false;
    check(inside, "particles are born in their box and fall from it");
}

void same_calls_same_particles() {
    particles::System a(5000), b(5000);
    static_cast<void>(a.add_emitter(fountain()));
    static_cast<void>(b.add_emitter(fountain()));
    a.advance(1.5);
    b.advance(.7);
    b.advance(.3);
    b.advance(.5);
    check(a.count() == b.count(), "the same seconds in different calls make as many particles");
    bool same = a.count() == b.count();
    for (std::size_t i = 0; same && i < a.count(); ++i) {
        const particles::Vec p = a.positions()[i];
        const particles::Vec q = b.positions()[i];
        if (std::fabs(p.x - q.x) > 1e-5f || std::fabs(p.y - q.y) > 1e-5f)
            same = false;
    }
    check(same, "and the same particles in the same places");
}

void free_fall_and_floor() {
    particles::System system(10);
    particles::Emitter e;
    e.low = e.high = {0, 20, 0};
    e.rate = 60;  // one in the first step
    e.life = {10, 10};
    e.forces.floor = 0;
    static_cast<void>(system.add_emitter(e));
    system.advance(1.0 / 60);
    system.set_rate(0, 0);
    system.advance(1.0);
    const float y = system.positions()[0].y;
    // About 4.9 m in the second (Euler steps fall slightly further).
    check(y < 20 - 4.8f && y > 20 - 5.2f, "a particle falls about 4.9 m in its first second");
    system.advance(2.0);
    check(system.count() == 0, "it ends when it reaches its floor");
    particles::System again(10);
    static_cast<void>(again.add_emitter(e));
    again.advance(1.0 / 60);
    again.set_rate(0, 0);
    bool landed = false;
    for (int s = 0; s < 240 && !landed; ++s) {
        again.advance(1.0 / 60);
        landed = !again.landings().empty();
    }
    check(landed, "and its landing is reported");
}

void a_full_pool_skips() {
    particles::System system(100);
    static_cast<void>(system.add_emitter(fountain()));
    system.advance(1.0);
    check(system.count() == 100, "the pool fills");
    check(system.skipped() > 0, "and births beyond it are counted, not made");
}

particles::Lens lens_for(int width, int height) {
    particles::Lens lens;
    lens.eye = {0, 10, 0};
    lens.width = width;
    lens.height = height;
    lens.focal = height * .5f / std::tan(.5f);
    return lens;
}

void drawing_stays_in_bounds_and_composes() {
    particles::System system(5000);
    static_cast<void>(system.add_emitter(fountain()));
    system.advance(.8);
    const particles::Lens lens = lens_for(160, 120);
    std::vector<particles::Shade> shades(system.count(), particles::Shade{1, 1, 1, .8f});
    particles::Layer layer;
    particles::Splatter::draw(system, shades, lens, {}, particles::Splat{}, layer);
    bool sane = true;
    double covered = 0;
    for (std::size_t i = 0; i < layer.through.size(); ++i) {
        if (layer.through[i] < 0 || layer.through[i] > 1)
            sane = false;
        covered += 1 - layer.through[i];
    }
    check(sane, "what passes the streaks stays between nothing and everything");
    check(covered > 1, "streaks in view cover something");
    // Composed over black, covered pixels show the streaks' white, the rest stays black.
    std::vector<float> picture(static_cast<std::size_t>(160 * 120 * 3), 0.0f);
    particles::Splatter::compose(layer, picture);
    float brightest = 0;
    for (float v : picture)
        brightest = std::max(brightest, v);
    check(brightest > .3f && brightest <= 1.0001f, "white streaks over black compose to at most white");
    // Hidden behind something nearer, nothing is drawn.
    std::vector<float> near(static_cast<std::size_t>(160 * 120), 1.0f);
    particles::Layer hidden;
    particles::Splatter::draw(system, shades, lens, near, particles::Splat{}, hidden);
    double hidden_cover = 0;
    for (float t : hidden.through)
        hidden_cover += 1 - t;
    check(hidden_cover == 0, "particles behind a nearer surface are not drawn");
}

void glows_keep_their_light() {
    particles::System system(10);
    particles::Emitter e;
    e.low = e.high = {0, 10, 8};
    e.rate = 60;
    e.kind = particles::Kind::mist;
    e.size = {.5f, .5f};
    e.forces.gravity = 0;
    static_cast<void>(system.add_emitter(e));
    system.advance(1.0 / 60);
    const particles::Lens lens = lens_for(200, 200);
    std::vector<particles::Shade> shades(system.count(), particles::Shade{1, 1, 1, 1});
    particles::Layer layer;
    particles::Splatter::draw(system, shades, lens, {}, particles::Splat{}, layer);
    // However it was drawn (a large glow goes on the half-resolution grid), composed
    // over black its light is all there, once.
    std::vector<float> picture(static_cast<std::size_t>(200 * 200 * 3), 0.0f);
    particles::Splatter::compose(layer, picture);
    double total = 0;
    for (std::size_t i = 0; i < picture.size(); i += 3)
        total += picture[i];
    check(std::fabs(total - 1) < .03, "a glow adds its light once, spread but not lost");
}

} // namespace

int main() {
    births_follow_rate_and_box();
    same_calls_same_particles();
    free_fall_and_floor();
    a_full_pool_skips();
    drawing_stays_in_bounds_and_composes();
    glows_keep_their_light();
    if (failures == 0)
        std::printf("particles: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
