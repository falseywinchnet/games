#pragma once
// The aviary: a great Victorian palm house seen from low on its floor, looking up into the
// dome. Painted iron ribs, glass between them and the sky outside, a gallery round the far
// wall, palms whose crowns throw real shade, banana and tree-fern beds, stone flags, and
// the perch stands of the table's layout (perching.hpp) set through it in depth. The still
// part is drawn once a size and kept with its depth; a frame puts it back and draws the
// birds over it.
#include "birds.hpp"
#include "perching.hpp"
#include "platform/render.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace pt {

namespace aviary_detail {
struct Textures;
struct Builder;
}  // namespace aviary_detail

// The house's colours and its weather.
struct AviaryLook {
    const char* name;
    Col iron, iron_dark;            // the painted ironwork
    Col glass;                      // the glazing's tint (alpha is its strength)
    Col sky_top, sky_low;           // overhead and at the horizon
    Col cloud_lit, cloud_dark;
    Col trees;                      // the garden's trees beyond the glass
    Col sun_col, amb_col;
    V3 sun;                         // toward the sun
};
int aviary_looks();
const AviaryLook& aviary_look(int i);

struct AviaryState {
    std::vector<BirdPose> birds;
    std::vector<bool> shaded;       // per bird: sitting in the palm's shade
    int spot = -1;                  // a bird picked out by a soft light
    double confetti = 0;
};

class Aviary {
public:
    R3D r;
    // The frame's size; the birds are framed between the top band and the bottom margin.
    void resize(int w, int h, int top_band = 0, int bottom = 0);
    // A table's perches and the house's look; the still part is drawn again.
    void set(const Layout& layout, int look, std::uint64_t seed);
    [[nodiscard]] const Layout& layout() const { return layout_; }
    // Draws the birds over the still: only those that changed since the last call, and
    // those they overlap. False when nothing changed (the picture is as it was).
    bool render(const AviaryState& s, double t);
    void to_screen(V3 p, double& sx, double& sy) const;
    [[nodiscard]] int pick_bird(double sx, double sy, const AviaryState& s) const;
    // Where the player stands (birds turn toward it).
    static V3 eye();

private:
    Layout layout_;
    int look_ = 0;
    std::uint64_t seed_ = 1;
    int w_ = 0, h_ = 0, top_ = 0, bottom_ = 0;
    std::vector<float> still_rgb_, still_depth_;
    bool still_ok_ = false;
    // While the window is being resized the old still is stretched to the new size, and
    // drawn properly once the size has held for a few frames.
    int still_w_ = 0, still_h_ = 0;
    int stale_frames_ = -1;
    // What each bird looked like when last drawn, and where: a frame draws again only the
    // birds that changed, and those they overlap, over the still put back beneath them.
    struct Drawn { std::uint64_t key = 0; int x0 = 0, y0 = 0, x1 = 0, y1 = 0; };
    std::vector<Drawn> drawn_;
    bool birds_whole_ = false;   // the buffers hold the still and every bird as last drawn
    Drawn bird_box(const BirdPose& b) const;
    // the still part's textures and geometry, kept for the table: a new size only draws it again
    struct Built;
    std::shared_ptr<Built> built_;
    void frame_camera();
    void draw_still();
    void build_scene(aviary_detail::Builder& B, const aviary_detail::Textures& T, const AviaryLook& L);
    void rasterise(const aviary_detail::Builder& B);
};

}  // namespace pt
