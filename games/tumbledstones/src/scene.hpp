#pragma once
// Draws Tumbled Stones' frames: the tray, kept with the stones that are at rest in it and
// brought up to date only where a stone came or went; the stones in motion over it; the
// stone in the player's hand, lifted, drawn anew at every step while it turns over or
// round; the counts beside the tray and the panels for choosing a pull and for a win.
//
// The families' shapes and every stone's pictures are made here when first wanted and
// kept; making them is the expensive part, so it runs on every core when the view lends
// a crew (`Runner`).
#include "minerals.hpp"
#include "rules.hpp"
#include "shapes.hpp"
#include "stage.hpp"
#include "stone_render.hpp"
#include "tray.hpp"

#include "target.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ts {

// Runs `count` jobs; on every core when `run` is given, one by one otherwise.
struct Runner {
    void (*run)(void* crew, int count, void (*job)(void*, int) noexcept, void* context) = nullptr;
    void* crew = nullptr;
};

enum class Panel { none, choose, won };

// Everything a frame shows that the board and the motion do not.
struct Frame {
    const Session* session = nullptr;
    const Motion* motion = nullptr;
    int hover = -1;              // the socket under the pointer
    double hover_x = 0, hover_y = 0;  // the pointer, points
    double hover_lift = 0;       // the stone under it lifts a touch (0..1)
    double hover_spin = 0;       // and rocks, radians
    int cursor = -1;             // the keyboard's socket, shown when the keyboard was used last
    // The stone in hand: its serial (0 none), its socket, its orientation, and its pose
    // while it turns.
    std::uint32_t held = 0;
    int held_cell = -1;
    int held_orientation = 0;
    bool held_turning = false;
    Pose held_pose;
    // A stone being dragged to swap: its socket, where it is (points), the socket it would
    // go to, and whether it may.
    int drag_cell = -1;
    double drag_x = 0, drag_y = 0;
    int drag_to = -1;
    bool drag_allowed = false;
    Panel panel = Panel::none;
    int choosing = 7;            // the pull shown on the chooser
    bool can_cancel = false;     // a game is under way behind the chooser
    bool new_best = false;       // the win was a best
    int pressed_button = 0;      // 1 fewer, 2 more, 3 begin, 4 cancel
    bool help = false;           // the game's own help card (standalone; PlaySuite has its own)
};

class Scene {
public:
    Scene();
    ~Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    void set_runner(Runner runner);
    // A new size, scale or byte order: the tray drawn again and every picture made anew.
    void resize(const Layout& layout, double scale, render::Order order);
    // Notes what the frame shows that is not made yet: the families' shapes (every family
    // the board knows, the one waiting to come in too), the stones' minerals and their lit
    // pictures. Nothing heavy happens here.
    void want(const Frame& frame);
    // The work noted, in batches: begun and ended on the drawing thread, run on any thread
    // (a worker's, or the drawing thread's for a small one). A running batch touches only
    // its own entries; a frame shows only finished ones.
    [[nodiscard]] bool begin_batch();
    void run_batch() noexcept;
    void end_batch();
    [[nodiscard]] bool batch_open() const {
        return batch_open_;
    }
    // How much is waiting: pictures, and whether any family's shape is among it.
    [[nodiscard]] std::size_t pictures_waiting() const {
        return pending_pictures_.size() + pending_arts_.size();
    }
    [[nodiscard]] bool shapes_waiting() const {
        return !pending_shapes_.empty();
    }
    // Whether every stone on its way has its picture: a new game's pour waits for them.
    [[nodiscard]] bool pictures_ready(const Motion& motion) const;
    // Draws the whole frame into `target` (the surface's size), with what is ready.
    void draw(const render::Target& target, const Frame& frame);
    // Lets go of pictures of stones no longer in the tray (not while a batch runs).
    void forget_unused(const Session& session);
    [[nodiscard]] bool ready() const {
        return !tray_.empty();
    }

private:
    struct ShapeEntry {
        Family family;
        std::unique_ptr<Shape> shape;
        bool ready = false;
    };
    struct Art;
    struct PictureEntry;
    Layout layout_;
    double scale_ = 1;
    render::Order order_ = render::Order::bgra;
    int width_ = 0, height_ = 0;     // device pixels
    Runner runner_;
    std::vector<std::uint32_t> tray_;  // the empty tray
    std::vector<std::uint32_t> rest_;  // the tray with its resting stones
    // What rest_ shows in each socket: a stone's serial, orientation, family and mark.
    struct Shown {
        std::uint32_t serial = 0;
        int orientation = -1;
        std::uint32_t family = 0;
        bool swapped = false;
        bool drawn = false;
    };
    std::vector<Shown> rest_shown_;
    bool rest_valid_ = false;
    std::map<std::uint32_t, std::unique_ptr<ShapeEntry>> shapes_;   // by family seed
    std::map<std::uint64_t, std::unique_ptr<Art>> art_;             // by stone and family
    std::map<std::uint64_t, std::unique_ptr<PictureEntry>> pictures_;  // resting, by look
    std::map<std::uint64_t, std::unique_ptr<PictureEntry>> lifted_;    // held up, by look
    std::map<std::uint64_t, std::unique_ptr<PictureEntry>> hovered_;   // under the pointer, with its shine
    std::vector<ShapeEntry*> pending_shapes_;
    std::vector<Art*> pending_arts_;
    std::vector<PictureEntry*> pending_pictures_;
    std::vector<ShapeEntry*> batch_shapes_;
    std::vector<Art*> batch_arts_;
    std::vector<PictureEntry*> batch_pictures_;
    bool batch_open_ = false;
    bool batch_stale_ = false;       // the size changed while it ran: its pictures are thrown away
    double batch_radius_ = 30;
    StoneWork work_;                                                // for drawing on this thread
    StonePicture turning_;                                          // the held stone mid-turn

    [[nodiscard]] double radius() const;
    ShapeEntry& shape_for(const Family& family);
    Art& art_for(std::uint32_t serial, const Stone& look, const Family& family);
    PictureEntry& picture_for(std::map<std::uint64_t, std::unique_ptr<PictureEntry>>& kept, Art& art,
                              std::uint32_t serial, int orientation, std::uint32_t family, double lift,
                              bool shine = false);
    [[nodiscard]] const StonePicture* hovered(std::uint32_t serial, int orientation, std::uint32_t family) const;
    [[nodiscard]] std::uint32_t hovered_serial(const Frame& frame) const;
    [[nodiscard]] const StonePicture* resting(std::uint32_t serial, int orientation, std::uint32_t family) const;
    [[nodiscard]] const StonePicture* lifted(std::uint32_t serial, int orientation, std::uint32_t family) const;
    [[nodiscard]] const Art* art_ready(std::uint32_t serial, std::uint32_t family) const;
    [[nodiscard]] static std::uint64_t look_key(std::uint32_t serial, int orientation, std::uint32_t family);
    void refresh_rest(const Frame& frame);
    void draw_counts(const render::Target& target, const Frame& frame) const;
    void draw_panel(const render::Target& target, const Frame& frame) const;
    void draw_help(const render::Target& target) const;
    void draw_marks(const render::Target& target, render::Rect clip, const std::vector<int>& cells) const;
};

// The number's word, two to twenty: "Nine in one pull".
[[nodiscard]] std::string number_word(int n, bool capital);

}  // namespace ts
