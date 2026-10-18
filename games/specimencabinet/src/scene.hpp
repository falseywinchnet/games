#pragma once
// Draws Specimen Cabinet's frames: the table (the walnut cabinet with its sockets, the felt,
// the wood round them), kept; over it the tumbler and every stone at rest, kept too and
// made again only when something changed; over that what moves: a stone falling from the
// tumbler's mouth onto the pile, the stone in hand, the marks of a socket chosen or wrong,
// and the panels.
//
// The shapes, the stones' printed materials, their lit pictures and the felt's cloth are
// made here when first wanted and kept; making them is the expensive part, so it runs on a
// worker in batches, on every core when the view lends a crew (`Runner`).
#include "rules.hpp"
#include "stage.hpp"

#include "minerals.hpp"
#include "shapes.hpp"
#include "stone_render.hpp"
#include "target.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sc {

// Runs `count` jobs; on every core when `run` is given, one by one otherwise.
struct Runner {
    void (*run)(void* crew, int count, void (*job)(void*, int) noexcept, void* context) = nullptr;
    void* crew = nullptr;
};

enum class Panel { none, choose, won };

// Everything a frame shows that the session does not.
struct Frame {
    const Session* session = nullptr;
    bool stones = true;          // the session's stones shown (not before a first cabinet)
    int cursor = -1;             // the keyboard's socket, shown when the keyboard was used last
    int held = -1;               // the stone in hand, drawn lifted at (held_x, held_y) points
    double held_x = 0, held_y = 0;
    int target = -1;             // the socket it would go to
    bool target_ok = false;      // and whether it may
    const std::vector<int>* waiting = nullptr;  // stones dropped but not yet shown fallen, next first
    int falling = -1;            // a stone on its way from the tumbler's mouth to the pile
    double fall = 0;             // 0 at the mouth .. 1 lying on the pile
    double turn = 0;             // how far the tumbler has turned, radians
    bool clashes = false;        // the cabinet is full and wrong: its clashes marked
    Panel panel = Panel::none;
    int choosing = 1;            // the size shown chosen on the chooser (an index into sizes)
    bool can_cancel = false;     // a cabinet is under way behind the chooser
    bool new_best = false;       // the solve was a best
    int pressed = -1;            // a panel button held down: 0..2 sizes, 3 cancel, 4 again
    bool help = false;           // the game's own help card (standalone; PlaySuite has its own)
};

class Scene {
public:
    Scene();
    ~Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    void set_runner(Runner runner);
    // The felt's cloth: the card tables' own tile (opaque BGRA bytes, `side` square), until
    // which the felt is plain.
    void set_felt(const std::vector<std::byte>& bgra, int side);
    // A new size, scale or byte order: the table drawn again, the pictures made anew.
    void resize(const Layout& layout, double scale, render::Order order);
    // Notes what is not made yet for the session shown: its shapes, its stones' materials
    // and pictures, the felt. Nothing heavy happens here.
    void want(const Frame& frame);
    // The work noted, in batches: begun and ended on the drawing thread, run on any thread.
    // A running batch touches only its own entries; a frame shows only finished ones.
    [[nodiscard]] bool begin_batch();
    void run_batch() noexcept;
    void end_batch();
    [[nodiscard]] bool batch_open() const {
        return batch_open_;
    }
    [[nodiscard]] bool work_waiting() const;
    // Every stone of the session has its picture: the table can be shown.
    [[nodiscard]] bool pictures_ready(const Session& session) const;
    // Draws the whole frame into `target` (the surface's size), with what is ready.
    void draw(const render::Target& target, const Frame& frame);
    [[nodiscard]] bool ready() const {
        return !table_.empty();
    }

private:
    struct ShapeEntry {
        std::uint32_t seed = 0;
        int style = 0;
        std::unique_ptr<stones::Shape> shape;
        bool ready = false;
    };
    struct Art {
        ShapeEntry* shape = nullptr;
        stones::Finish finish;
        stones::MineralPicture picture;
        bool ready = false;
    };
    struct PictureEntry {
        const Art* art = nullptr;
        int orientation = 0;
        double lift = 0;
        stones::StonePicture picture;
        bool ready = false;
    };
    Layout layout_;
    double scale_ = 1;
    render::Order order_ = render::Order::bgra;
    int width_ = 0, height_ = 0;     // device pixels
    Runner runner_;
    std::vector<std::uint32_t> table_;   // the wood, the cabinet's sockets and the felt
    bool table_has_felt_ = false;        // the felt drawn from its cloth, not plain
    std::vector<std::uint32_t> rest_;    // the table with the tumbler and the stones at rest
    std::uint64_t rest_key_ = 0;         // what rest_ shows
    bool rest_valid_ = false;
    std::map<std::uint32_t, std::unique_ptr<ShapeEntry>> shapes_;        // by family seed
    std::map<std::uint64_t, std::unique_ptr<Art>> arts_;                 // by cabinet and stone
    std::map<std::uint64_t, std::unique_ptr<PictureEntry>> pictures_;    // resting, by cabinet, stone and turn
    std::map<std::uint64_t, std::unique_ptr<PictureEntry>> lifted_;      // held up
    std::vector<ShapeEntry*> pending_shapes_;
    std::vector<Art*> pending_arts_;
    std::vector<PictureEntry*> pending_pictures_;
    std::vector<ShapeEntry*> batch_shapes_;
    std::vector<Art*> batch_arts_;
    std::vector<PictureEntry*> batch_pictures_;
    std::vector<std::byte> felt_tile_;   // opaque BGRA, felt_side_ square
    int felt_side_ = 0;
    bool batch_open_ = false;
    bool batch_stale_ = false;           // the size changed while it ran: its pictures go
    double batch_radius_ = 30;
    std::uint64_t cabinet_ = 0;          // the session whose stones are kept

    [[nodiscard]] double radius() const;
    void forget_all();
    ShapeEntry& shape_for(std::uint32_t seed, int style);
    Art& art_for(const Puzzle& puzzle, int stone);
    PictureEntry& picture_for(std::map<std::uint64_t, std::unique_ptr<PictureEntry>>& kept, Art& art, int stone, int orientation,
                              double lift);
    [[nodiscard]] const stones::StonePicture* resting(int stone, int orientation) const;
    [[nodiscard]] const stones::StonePicture* lifted(int stone, int orientation) const;
    [[nodiscard]] std::uint64_t key(int stone, int orientation) const;
    void make_table();
    void compose_rest(const Frame& frame, std::uint64_t key);
    [[nodiscard]] std::uint64_t rest_signature(const Frame& frame) const;
    void draw_marks(const render::Target& target, const Frame& frame) const;
    void draw_counts(const render::Target& target, const Frame& frame) const;
    void draw_panel(const render::Target& target, const Frame& frame) const;
    void draw_help(const render::Target& target) const;
};

}  // namespace sc
