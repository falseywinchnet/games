#pragma once
// The falls as a still picture in linear light: the scene's geometry and its look,
// evaluated sample by sample. It is the reference the moving scene is baked from
// (every fixed layer is a part of it) and what candidates of a look are judged by.
//
// The place: a wooden walkway through the canopy, 55 m in front of a basalt wall
// over which a section of a great river falls 40 m into a pool. The sun stands above
// the brink, a little to the right, so the falling water is lit from behind: the
// crest glows, droplets throw the sun's glints toward the eye, and the rock face is
// in its own shade, lit by the sky and by the white water beside it.
#include "falls_math.hpp"
#include "sky.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace cf {

struct Look {
    SkyParameters sky{};
    double exposure = 6.8;        // linear scale before the tone curve
    double water_amber = 1;       // how strongly the river's sediment tints thin water
    double glints = 1;            // the density of the sun's glints in the falling water
    double crest_haze = 1;        // the sunlit spray round the brink
    double cloud = 1;             // light cloud behind the viewer: its soft white fill
    std::uint32_t seed = 1;
};

struct Camera {
    double eye_height = 13.7;     // metres above the pool
    double pitch = 5 * pi / 180;  // the view's tilt upward
    double fov = 100 * pi / 180;  // vertical field of view
    // The lens's bend (particles::Lens): wide enough to hold a high sun and the deck at
    // one's feet in one picture, bent halfway to stereographic so shapes keep true
    // without long lines bowing much; 0 for a pinhole (narrow views: the crest, the sky).
    double bend = .55;
    double yaw = 0;               // the view's turn to the right
};

struct LinearImage {
    int width = 0, height = 0;
    std::vector<Rgb> pixels;  // width * height, row-major from the top
};

// Draws rows [first_row, last_row) of a width x height picture of the falls at
// `time` seconds into `out` (which it sizes), `samples` x `samples` per pixel.
void render_still(const Look& look, const Camera& camera, int width, int height, int first_row,
                  int last_row, double time, int samples, LinearImage& out);
// The sky alone, as `camera` sees it, with the sun and its glare: no land or water.
void render_sky(const Look& look, const Camera& camera, int width, int height, LinearImage& out);
// The falls as the live scene draws them. What does not move is baked once per picture
// size (off the window's thread, in bands that may be baked at once); a frame then
// draws the falling sheet from a loop of its own noise, the sun's glitter on the pool,
// and the particles, and puts the tone curve on it.
// Runs `count` jobs, each once and in any order, on as many threads as it likes, and
// returns when all are done; `runner` is its own. The view lends one over its pool;
// without one the jobs run here in turn.
using Runner = void (*)(void* runner, int count, void (*job)(void* context, int index) noexcept, void* context);

class LiveFalls final {
  public:
    LiveFalls(const Look& look, const Camera& camera);
    ~LiveFalls();
    LiveFalls(const LiveFalls&) = delete;
    LiveFalls& operator=(const LiveFalls&) = delete;

    // Once, before the first size is finished (any thread): the sky, the light, the
    // planting, the sheet's noise, and the water run until it has reached the pool.
    // The sky and the noise are spread over `run` when there is one. Later calls do
    // nothing.
    void prepare_once(Runner run = nullptr, void* runner = nullptr);
    // A new picture size: begin it (the window's thread), bake every band (any threads,
    // each band once), finish it (any one thread, after every band), then adopt it (the
    // window's thread). The size shown goes on drawing until it is adopted.
    void begin_size(int width, int height, int samples);
    [[nodiscard]] int bands() const;
    void bake_band(int band);
    void finish_size();
    void adopt();
    [[nodiscard]] bool ready() const;
    void size(int& width, int& height) const;

    // How many particles the falls make: 1 as planted, less for a lighter detail.
    void set_density(double density);
    // Moves the water on (the window's thread).
    void advance(double seconds);
    // The picture now: width * height words, 0xFFRRGGBB, row-major (the window's thread).
    void draw(std::vector<std::uint32_t>& out);
    [[nodiscard]] std::size_t particle_count() const;
    [[nodiscard]] double time() const;
    // For tools: seconds a frame spends on average copying, on the sheet, the glitter,
    // shading the particles, drawing them, and the tone curve.
    void stage_costs(double out[6]) const;

    struct State;

  private:
    std::unique_ptr<State> state_;
};

// Spreads the brightest points' light (the glints) as the eye does.
void add_bloom(LinearImage& image, double threshold);
// Exposure, the tone curve and sRGB: blue, green, red, alpha bytes per pixel.
void tone_map(const LinearImage& image, double exposure, std::vector<std::uint8_t>& bgra);

} // namespace cf
