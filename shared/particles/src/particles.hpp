#pragma once
// Particles for PlaySuite scenes: water breaking from a falling sheet, drops, spray,
// mist, sparks, leaves; anything made of many small things that each go their own
// way under a few forces.
//
// A System owns a fixed pool of particles stored as arrays (one per attribute), the
// emitters that make them (data: where, how many a second, how fast, how long they
// live) and the forces on them (gravity, drag, lift, a smooth swirling wind, a
// floor). It is deterministic: the n-th particle an emitter makes draws every random
// choice from a hash of the emitter's seed and n, and the simulation steps at a fixed
// interval, so the same calls give the same particles on every machine.
//
// A Splatter draws them through a pinhole lens into a light buffer the size of the
// picture: streaks (a head with a trailing tail, motion-blurred) that cover what lies
// behind them, mist (soft discs) and glints (points) that add light. Coverage is
// accumulated independently of drawing order, so particles need no sorting. Each
// particle's colour comes from the game (a Shade per particle), because only the game
// knows its light; the engine knows nothing of a scene.
//
// No clock, threads or GUI.Forms: time arrives as seconds.
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace particles {

enum class Kind : std::uint8_t {
    streak,  // covers what is behind it: water, leaves
    mist,    // adds light, soft and large
    glint,   // adds light at a point
};

struct Vec {
    float x = 0, y = 0, z = 0;
};

struct Range {
    float low = 0, high = 0;
};

// What acts on an emitter's particles.
struct Forces {
    float gravity = 9.81f;          // m/s^2, downward
    float drag = 0;                 // per second: the pull toward the air's own motion
    float lift = 0;                 // m/s^2 upward (warm or buoyant spray)
    float swirl = 0;                // m/s: the air's swirling motion, smooth in space and time
    float swirl_scale = 4;          // metres across one swirl
    float swirl_period = 3;         // seconds for the swirl to change
    float floor = -1e30f;           // height at which particles end (a pool's surface)
    float growth = 0;               // metres per second the size grows (mist spreading)
};

// Where and how particles are born. They appear uniformly in the box from `low` to
// `high` (world metres), `rate` a second, with a velocity drawn about `velocity` by
// `velocity_spread` in each axis.
struct Emitter {
    Vec low{}, high{};
    float rate = 0;
    Vec velocity{}, velocity_spread{};
    Range life{1, 1};               // seconds
    Range size{.1f, .1f};           // metres: a streak's width, a mist puff's radius
    Range tail{0, 0};               // seconds of path a streak trails behind its head
    Kind kind = Kind::streak;
    std::uint32_t seed = 1;
    Forces forces{};
};

class System final {
  public:
    // At most `capacity` particles live at once; births beyond it are skipped (and
    // counted, so a scene can see it asks too much).
    explicit System(std::size_t capacity);

    // Returns the emitter's number.
    int add_emitter(const Emitter& emitter);
    // Births a second, for surges and lulls; 0 stops it.
    void set_rate(int emitter, float rate);
    [[nodiscard]] const Emitter& emitter(int index) const {
        return emitters_[static_cast<std::size_t>(index)].spec;
    }
    [[nodiscard]] int emitter_count() const {
        return static_cast<int>(emitters_.size());
    }

    // Moves time on by `seconds` in fixed steps of `step` (the remainder is carried to
    // the next call), making and ending particles as it goes.
    void advance(double seconds, double step = 1.0 / 60);
    // Seconds simulated so far.
    [[nodiscard]] double time() const {
        return time_;
    }

    // The particles, as arrays of count() entries. Order is not stable across steps.
    [[nodiscard]] std::size_t count() const {
        return count_;
    }
    [[nodiscard]] std::span<const Vec> positions() const {
        return {position_.data(), count_};
    }
    [[nodiscard]] std::span<const Vec> velocities() const {
        return {velocity_.data(), count_};
    }
    [[nodiscard]] std::span<const float> ages() const {
        return {age_.data(), count_};
    }
    [[nodiscard]] std::span<const float> lives() const {
        return {life_.data(), count_};
    }
    [[nodiscard]] std::span<const float> sizes() const {
        return {size_.data(), count_};
    }
    [[nodiscard]] std::span<const float> tails() const {
        return {tail_.data(), count_};
    }
    [[nodiscard]] std::span<const Kind> kinds() const {
        return {kind_.data(), count_};
    }
    [[nodiscard]] std::span<const std::uint16_t> origins() const {
        return {origin_.data(), count_};
    }
    // A per-particle random number, fixed for its life (twinkle phases, tints).
    [[nodiscard]] std::span<const std::uint32_t> seeds() const {
        return {seed_.data(), count_};
    }
    // Births skipped for want of room since the system was made.
    [[nodiscard]] std::uint64_t skipped() const {
        return skipped_;
    }
    // Where particles reached their floor during the last advance(), for splashes.
    [[nodiscard]] const std::vector<Vec>& landings() const {
        return landings_;
    }

  private:
    struct Source {
        Emitter spec{};
        double owed = 0;            // births due but not yet made
        std::uint64_t born = 0;     // births so far: the next particle's number
    };
    std::vector<Source> emitters_;
    std::size_t capacity_ = 0;
    std::size_t count_ = 0;
    std::vector<Vec> position_, velocity_;
    std::vector<float> age_, life_, size_, tail_;
    std::vector<Kind> kind_;
    std::vector<std::uint16_t> origin_;
    std::vector<std::uint32_t> seed_;
    std::vector<Vec> landings_;
    double time_ = 0;
    double carried_ = 0;
    std::uint64_t skipped_ = 0;

    void step(float seconds);
    void birth(Source& source, std::size_t emitter_index, float into_step);
    void remove(std::size_t index);
};

// A camera: an eye, its basis, and the picture it makes, through a lens of the general
// perspective family: a point `angle` from the view's centre falls
// (bend + 1) focal sin(angle) / (bend + cos(angle)) pixels from the picture's centre.
// bend 0 is a pinhole (straight lines stay straight, the edges stretch); bend 1 is
// stereographic (shapes stay true over a very wide view, long lines bow); between,
// a balance of the two.
struct Lens {
    Vec eye{};
    Vec forward{0, 0, 1}, right{1, 0, 0}, up{0, 1, 0};
    float focal = 1;                // pixels
    float bend = 0;
    int width = 0, height = 0;
};

// Where a point falls in the picture: pixel coordinates, its distance (along the view
// for a pinhole, along the ray for any bent lens, as the scene's depths are measured)
// and how many pixels a metre spans there.
struct Projected {
    float x = 0, y = 0, depth = 0, scale = 0;
    bool in_front = false;
};
[[nodiscard]] Projected project(const Lens& lens, Vec point);

// A particle's colour as the game lights it: linear light. For a streak, `alpha` is
// its opacity at its head; for mist and glints it scales the light added.
struct Shade {
    float r = 0, g = 0, b = 0, alpha = 0;
};

// The light buffer particles are drawn into, width * height pixels row-major from the
// top. Streaks accumulate a coverage-weighted colour and the light that passes all of
// them; mist and glints accumulate added light.
struct Layer {
    int width = 0, height = 0;
    std::vector<float> colour;       // 3 per pixel: sum of colour x coverage
    std::vector<float> coverage;     // sum of coverage
    std::vector<float> through;      // product of (1 - coverage): what passes them all
    std::vector<float> added;        // 3 per pixel
    // Large soft glows are drawn on a grid of a quarter the resolution (they have no
    // detail to lose) and smoothed back up as the layer is composed: 3 per coarse pixel.
    static constexpr int coarse_step = 4;
    int coarse_width = 0, coarse_height = 0;
    std::vector<float> coarse;
    // The pixels drawn on since the last reset (half-open, fine pixels; empty when
    // left >= right): by streaks and small glows, by large glows, and both together.
    // Only these are cleared again and composed.
    int fine_left = 0, fine_top = 0, fine_right = 0, fine_bottom = 0;
    int glow_left = 0, glow_top = 0, glow_right = 0, glow_bottom = 0;
    int dirty_left = 0, dirty_top = 0, dirty_right = 0, dirty_bottom = 0;
    // Ready for a new frame of `width` x `height`: everything cleared at a new size,
    // only what was drawn on at the same size.
    void reset(int width, int height);
    void mark(int left, int top, int right, int bottom);
    void mark_glow(int left, int top, int right, int bottom);
};

// How particles are drawn.
struct Splat {
    float exposure = 1.0f / 30;      // seconds of motion blur
    float head = .7f;                // how much of a streak's opacity gathers at its head
    float glint_size = .7f;          // pixels: the core of a glint
    float softness = .6f;            // how much of a streak's half-width fades to its edge
    float taper = .45f;              // a streak's width at its tail, as a share of its head's
    float fray = 0;                  // how much a streak breaks into beads along its length
    float fray_length = .25f;        // metres between the beads
};

class Splatter final {
  public:
    // Draws every particle of `system` with `shades` (one per particle, in the
    // system's order) through `lens` into `layer`, hidden where `depth` (if not
    // empty, width * height) is nearer.
    static void draw(const System& system, std::span<const Shade> shades, const Lens& lens,
                     std::span<const float> depth, const Splat& settings, Layer& layer);
    // Lays `layer` over `picture` (3 floats per pixel, linear light, the same size):
    // the streaks' average colour over what they cover, then the added light.
    static void compose(const Layer& layer, std::span<float> picture);
};

} // namespace particles
