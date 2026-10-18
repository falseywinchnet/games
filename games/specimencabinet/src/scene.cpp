#include "scene.hpp"

#include "noise.hpp"
#include "platform/text.hpp"
#include "r2d.hpp"
#include "tray.hpp"
#include "tumbler.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sc {
namespace {

using render::r2d::Col;

const Col cream = render::r2d::hex(0xeee2c8);
const Col quiet = render::r2d::hex(0xc8b898);
const Col gold = render::r2d::hex(0xe2b35a);
const Col ink = render::r2d::hex(0x0d0805);
constexpr int picture_side = 112;  // a material's texels across a face

render::Color colour(Col c, float a = 1) {
    return render::Color{c.r * 255, c.g * 255, c.b * 255, a * 255};
}

// Text at (x, y) device pixels, its left edge, centre or right edge there (align 0, 1,
// 2), with a soft shadow beneath. Returns its height.
int put_text(const render::Target& target, render::Rect clip, const std::string& text, Font font, double size,
             double x, double y, Col c, int align, double wrap = 0) {
    if (text.empty() || size < 4)
        return 0;
    const Mask& mask = text_mask(text, font, size, wrap);
    int left = static_cast<int>(std::lround(x));
    if (align == 1)
        left -= mask.w / 2;
    else if (align == 2)
        left -= mask.w;
    const int top = static_cast<int>(std::lround(y));
    const int drop = std::max(1, static_cast<int>(size / 14));
    render::r2d::blit_text(target, clip, mask, left + drop, top + drop, 1, render::r2d::alpha(ink, .7F));
    render::r2d::blit_text(target, clip, mask, left, top, 1, c);
    return mask.h;
}

std::string clock_text(double seconds) {
    const int total = static_cast<int>(seconds);
    const int hours = total / 3600;
    const int minutes = (total / 60) % 60;
    const int secs = total % 60;
    char text[32];
    if (hours > 0)
        std::snprintf(text, sizeof text, "%d:%02d:%02d", hours, minutes, secs);
    else
        std::snprintf(text, sizeof text, "%d:%02d", minutes, secs);
    return text;
}

struct ShapeJob {
    std::uint32_t seed;
    int style;
    std::unique_ptr<stones::Shape>* out;
};
struct ShapeRound {
    std::vector<ShapeJob> jobs;
};
void make_one_shape(void* context, int index) noexcept {
    ShapeJob& job = (*static_cast<ShapeRound*>(context)).jobs[static_cast<std::size_t>(index)];
    try {
        const stones::Style style = static_cast<stones::Style>(std::clamp(job.style, 0, stones::style_count - 1));
        *job.out = std::make_unique<stones::Shape>(stones::Shape::make(job.seed, style));
    } catch (...) {
        *job.out = std::make_unique<stones::Shape>(stones::Shape::make(1, stones::Style::facet));
    }
}

struct PrintJob {
    const stones::Shape* shape;
    const stones::Finish* finish;
    stones::MineralPicture* picture;
};
struct PrintRound {
    std::vector<PrintJob> jobs;
};
void print_one(void* context, int index) noexcept {
    PrintJob& job = (*static_cast<PrintRound*>(context)).jobs[static_cast<std::size_t>(index)];
    try {
        stones::print_mineral(*job.finish, *job.shape, picture_side, *job.picture);
    } catch (...) {
        (*job.picture).side = 0;
    }
}

struct DrawJob {
    stones::StoneLook look;
    int orientation = 0;
    double lift = 0;
    stones::StonePicture* out = nullptr;
};
struct DrawRound {
    std::vector<DrawJob> jobs;
    double radius = 30;
};
void draw_one(void* context, int index) noexcept {
    DrawRound& round = *static_cast<DrawRound*>(context);
    DrawJob& job = round.jobs[static_cast<std::size_t>(index)];
    try {
        stones::StoneWork work;
        stones::DrawStone how;
        how.radius = round.radius;
        how.lift = job.lift;
        how.fineness = 2;
        stones::draw_stone(job.look, stones::orientation_pose(job.orientation), how, work, *job.out);
    } catch (...) {
        (*job.out) = stones::StonePicture{};
    }
}

struct TableJob {
    const stones::TrayPlan* plan;
    std::uint32_t* pixels;
    bool bgra;
    int bands;
};
void table_band(void* context, int index) noexcept {
    const TableJob& job = *static_cast<const TableJob*>(context);
    const int rows = (*job.plan).height;
    stones::draw_tray(*job.plan, job.pixels, (*job.plan).width, job.bgra, rows * index / job.bands,
                      rows * (index + 1) / job.bands);
}

void run_jobs(const Runner& runner, int count, void (*job)(void*, int) noexcept, void* context) {
    if (count <= 0)
        return;
    if (runner.run != nullptr) {
        runner.run(runner.crew, count, job, context);
        return;
    }
    for (int i = 0; i < count; ++i)
        job(context, i);
}

std::uint64_t mix(std::uint64_t hash, std::uint64_t value) {
    return (hash ^ value) * 1099511628211ULL;
}

bool has_puzzle(const Session& session) {
    return session.puzzle.family_seed[0] != 0;
}

bool showing(const Frame& frame) {
    return frame.session != nullptr && frame.stones && has_puzzle(*frame.session);
}

}  // namespace

Scene::Scene() = default;
Scene::~Scene() = default;

void Scene::set_runner(Runner runner) {
    runner_ = runner;
}

void Scene::set_felt(const std::vector<std::byte>& bgra, int side) {
    if (side <= 0 || bgra.size() < static_cast<std::size_t>(side) * static_cast<std::size_t>(side) * 4)
        return;
    felt_tile_ = bgra;
    felt_side_ = side;
    if (!table_.empty())
        make_table();
}

double Scene::radius() const {
    return layout_.stone * .5 * scale_;
}

std::uint64_t Scene::key(int stone, int orientation) const {
    return mix(mix(cabinet_, static_cast<std::uint64_t>(stone) + 1), static_cast<std::uint64_t>(orientation) + 101);
}

void Scene::forget_all() {
    arts_.clear();
    pictures_.clear();
    lifted_.clear();
    pending_arts_.clear();
    pending_pictures_.clear();
    rest_valid_ = false;
}

void Scene::resize(const Layout& layout, double scale, render::Order order) {
    const bool same_stones = layout.stone == layout_.stone && scale == scale_;
    layout_ = layout;
    scale_ = scale;
    order_ = order;
    width_ = std::max(1, static_cast<int>(std::lround(layout.width * scale)));
    height_ = std::max(1, static_cast<int>(std::lround(layout.height * scale)));
    make_table();
    if (!same_stones) {
        if (batch_open_) {
            batch_stale_ = true;
        } else {
            pictures_.clear();
            lifted_.clear();
            pending_pictures_.clear();
        }
    }
}

// The wood all over, the cabinet's sockets in figured walnut, and the felt let into it.
void Scene::make_table() {
    const double s = scale_;
    stones::TrayPlan plan;
    plan.width = width_;
    plan.height = height_;
    plan.scale = s;
    plan.grid_x = layout_.cabinet.x * s;
    plan.grid_y = layout_.cabinet.y * s;
    plan.cell = layout_.cell * s;
    plan.columns = layout_.n;
    plan.rows = layout_.n;
    plan.wood = 3;
    plan.wall = 10 * s;
    table_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), 0U);
    TableJob job{&plan, table_.data(), order_ == render::Order::bgra, 16};
    run_jobs(runner_, job.bands, table_band, &job);
    // The felt: the card tables' cloth, or plain baize until it comes, sunk a little where
    // the wood's edge overhangs it.
    const Box& f = layout_.felt;
    const double radius = 10 * s;
    const int red_shift = order_ == render::Order::bgra ? 16 : 0;
    const int blue_shift = order_ == render::Order::bgra ? 0 : 16;
    const bool cloth = felt_side_ > 0;
    for (int y = static_cast<int>(f.y * s); y < static_cast<int>((f.y + f.h) * s) && y < height_; ++y)
        for (int x = static_cast<int>(f.x * s); x < static_cast<int>((f.x + f.w) * s) && x < width_; ++x) {
            if (x < 0 || y < 0)
                continue;
            const double dx = std::max({f.x * s + radius - x, 0.0, x - ((f.x + f.w) * s - radius)});
            const double dy = std::max({f.y * s + radius - y, 0.0, y - ((f.y + f.h) * s - radius)});
            if (dx * dx + dy * dy > radius * radius)
                continue;
            double r = 26, g = 84, b = 58;
            if (cloth) {
                const std::size_t at = (static_cast<std::size_t>(y % felt_side_) * static_cast<std::size_t>(felt_side_) +
                                        static_cast<std::size_t>(x % felt_side_)) * 4;
                b = static_cast<double>(felt_tile_[at]);
                g = static_cast<double>(felt_tile_[at + 1]);
                r = static_cast<double>(felt_tile_[at + 2]);
            }
            const double inset = std::min({(x - f.x * s) / (12 * s), (y - f.y * s) / (12 * s), 1.0});
            const double light = .5 + .5 * inset;
            table_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)] =
                (static_cast<std::uint32_t>(r * light) << red_shift) | (static_cast<std::uint32_t>(g * light) << 8) |
                (static_cast<std::uint32_t>(b * light) << blue_shift) | 0xff000000U;
        }
    table_has_felt_ = cloth;
    rest_.assign(table_.size(), 0U);
    rest_valid_ = false;
}

Scene::ShapeEntry& Scene::shape_for(std::uint32_t seed, int style) {
    std::unique_ptr<ShapeEntry>& slot = shapes_[seed];
    if (!slot) {
        slot = std::make_unique<ShapeEntry>();
        (*slot).seed = seed;
        (*slot).style = style;
        pending_shapes_.push_back(slot.get());
    }
    return *slot;
}

Scene::Art& Scene::art_for(const Puzzle& puzzle, int stone) {
    std::unique_ptr<Art>& slot = arts_[key(stone, 1000)];
    if (!slot) {
        slot = std::make_unique<Art>();
        Art& art = *slot;
        const std::size_t shape = static_cast<std::size_t>(stone_shape(puzzle, stone));
        const std::size_t material = static_cast<std::size_t>(stone_material(puzzle, stone));
        art.shape = &shape_for(puzzle.family_seed[shape], puzzle.family_style[shape]);
        // Every stone of a material is that material, alike in colour; its own shape
        // shows a different part of the pattern.
        art.finish = stones::make_finish(static_cast<stones::Mineral>(std::clamp(puzzle.material_kind[material], 0, stones::mineral_count - 1)),
                                         puzzle.material_seed[material]);
        pending_arts_.push_back(&art);
    }
    return *slot;
}

Scene::PictureEntry& Scene::picture_for(std::map<std::uint64_t, std::unique_ptr<PictureEntry>>& kept, Art& art, int stone,
                                        int orientation, double lift) {
    std::unique_ptr<PictureEntry>& slot = kept[key(stone, orientation)];
    if (!slot) {
        slot = std::make_unique<PictureEntry>();
        (*slot).art = &art;
        (*slot).orientation = orientation;
        (*slot).lift = lift;
        pending_pictures_.push_back(slot.get());
    }
    return *slot;
}

const stones::StonePicture* Scene::resting(int stone, int orientation) const {
    std::map<std::uint64_t, std::unique_ptr<PictureEntry>>::const_iterator found = pictures_.find(key(stone, orientation));
    if (found == pictures_.end() || !(*(*found).second).ready)
        return nullptr;
    return &(*(*found).second).picture;
}

const stones::StonePicture* Scene::lifted(int stone, int orientation) const {
    std::map<std::uint64_t, std::unique_ptr<PictureEntry>>::const_iterator found = lifted_.find(key(stone, orientation));
    if (found == lifted_.end() || !(*(*found).second).ready)
        return nullptr;
    return &(*(*found).second).picture;
}

bool Scene::pictures_ready(const Session& session) const {
    if (table_.empty())
        return false;
    if (!has_puzzle(session))
        return true;
    const int count = session.puzzle.n * session.puzzle.n;
    for (int stone = 0; stone < count; ++stone)
        if (resting(stone, session.turn[static_cast<std::size_t>(stone)]) == nullptr)
            return false;
    return true;
}

bool Scene::work_waiting() const {
    return !pending_shapes_.empty() || !pending_arts_.empty() || !pending_pictures_.empty();
}

void Scene::want(const Frame& frame) {
    if (!showing(frame))
        return;
    const Session& session = *frame.session;
    const Puzzle& puzzle = session.puzzle;
    const std::uint64_t cabinet = mix(mix(1469598103934665603ULL, puzzle.seed), static_cast<std::uint64_t>(puzzle.n));
    if (cabinet != cabinet_) {
        // Another cabinet: what was made for the last goes (once no batch is using it).
        if (batch_open_)
            batch_stale_ = true;
        else
            forget_all();
        cabinet_ = cabinet;
    }
    if (table_.empty())
        return;
    const int count = puzzle.n * puzzle.n;
    for (int stone = 0; stone < count; ++stone) {
        Art& art = art_for(puzzle, stone);
        static_cast<void>(picture_for(pictures_, art, stone, session.turn[static_cast<std::size_t>(stone)], 0));
    }
    if (frame.held >= 0 && frame.held < count) {
        Art& art = art_for(puzzle, frame.held);
        static_cast<void>(picture_for(lifted_, art, frame.held, session.turn[static_cast<std::size_t>(frame.held)], 1));
    }
}

bool Scene::begin_batch() {
    if (batch_open_ || !work_waiting())
        return false;
    batch_shapes_.swap(pending_shapes_);
    batch_arts_.swap(pending_arts_);
    batch_pictures_.swap(pending_pictures_);
    pending_shapes_.clear();
    pending_arts_.clear();
    pending_pictures_.clear();
    batch_radius_ = radius();
    batch_open_ = true;
    batch_stale_ = false;
    return true;
}

void Scene::run_batch() noexcept {
    // The shapes first, then the materials printed through them, then the lit pictures.
    ShapeRound shapes;
    for (ShapeEntry* entry : batch_shapes_)
        shapes.jobs.push_back({(*entry).seed, (*entry).style, &(*entry).shape});
    run_jobs(runner_, static_cast<int>(shapes.jobs.size()), make_one_shape, &shapes);
    PrintRound prints;
    for (Art* art : batch_arts_)
        if ((*(*art).shape).shape != nullptr)
            prints.jobs.push_back({(*(*art).shape).shape.get(), &(*art).finish, &(*art).picture});
    run_jobs(runner_, static_cast<int>(prints.jobs.size()), print_one, &prints);
    DrawRound draws;
    draws.radius = batch_radius_;
    for (PictureEntry* entry : batch_pictures_) {
        const Art& art = *(*entry).art;
        if ((*art.shape).shape == nullptr || art.picture.side == 0)
            continue;
        draws.jobs.push_back({stones::StoneLook{(*art.shape).shape.get(), &art.finish, &art.picture}, (*entry).orientation,
                              (*entry).lift, &(*entry).picture});
    }
    run_jobs(runner_, static_cast<int>(draws.jobs.size()), draw_one, &draws);
}

void Scene::end_batch() {
    if (!batch_open_)
        return;
    for (ShapeEntry* entry : batch_shapes_)
        (*entry).ready = (*entry).shape != nullptr;
    for (Art* art : batch_arts_)
        (*art).ready = (*art).picture.side > 0;
    for (PictureEntry* entry : batch_pictures_)
        (*entry).ready = true;
    batch_shapes_.clear();
    batch_arts_.clear();
    batch_pictures_.clear();
    batch_open_ = false;
    rest_valid_ = false;
    if (batch_stale_) {
        // Made for another cabinet or another size: let it all go; what is wanted now is
        // asked for again by the next frame.
        batch_stale_ = false;
        forget_all();
        pending_shapes_.clear();
        for (std::pair<const std::uint32_t, std::unique_ptr<ShapeEntry>>& entry : shapes_)
            if (!(*entry.second).ready)
                pending_shapes_.push_back(entry.second.get());
    }
}

std::uint64_t Scene::rest_signature(const Frame& frame) const {
    std::uint64_t h = mix(1469598103934665603ULL, cabinet_);
    h = mix(h, table_has_felt_ ? 1 : 2);
    h = mix(h, static_cast<std::uint64_t>(std::lround(frame.turn * 100)));
    h = mix(h, static_cast<std::uint64_t>(frame.held + 7));
    h = mix(h, static_cast<std::uint64_t>(frame.falling + 7));
    if (!showing(frame))
        return h;
    const Session& session = *frame.session;
    const int count = session.puzzle.n * session.puzzle.n;
    int ready = 0;
    for (int stone = 0; stone < count; ++stone) {
        const Where& w = session.where[static_cast<std::size_t>(stone)];
        h = mix(h, static_cast<std::uint64_t>(w.place) * 131 + static_cast<std::uint64_t>(w.cell + 1));
        h = mix(h, static_cast<std::uint64_t>(std::lround(w.x * 4096)) * 8192 + static_cast<std::uint64_t>(std::lround(w.y * 4096)));
        h = mix(h, static_cast<std::uint64_t>(session.turn[static_cast<std::size_t>(stone)]));
        ready += resting(stone, session.turn[static_cast<std::size_t>(stone)]) != nullptr ? 1 : 0;
    }
    for (int stone : session.pile)
        h = mix(h, static_cast<std::uint64_t>(stone) + 3);
    if (frame.waiting != nullptr)
        for (int stone : *frame.waiting)
            h = mix(h, static_cast<std::uint64_t>(stone) + 11);
    for (std::size_t i = 0; i < session.drum.size() && i < 3; ++i)
        h = mix(h, static_cast<std::uint64_t>(session.drum[i]) + 5);
    return mix(h, static_cast<std::uint64_t>(ready));
}

void Scene::compose_rest(const Frame& frame, std::uint64_t signature) {
    rest_ = table_;
    rest_key_ = signature;
    rest_valid_ = true;
    const render::Target target{rest_.data(), width_, width_, height_, order_};
    const double s = scale_;
    const bool bgra = order_ == render::Order::bgra;
    TumblerLook drum;
    drum.x = layout_.mouth_x * s;
    drum.y = layout_.mouth_y * s;
    drum.radius = layout_.drum * s;
    drum.turn = frame.turn;
    drum.scale = s;
    draw_tumbler(target, drum, TumblerPass::body);
    const bool stones = showing(frame);
    if (stones) {
        // The next few stones, glimpsed inside the drum.
        const Session& session = *frame.session;
        std::vector<int> inner;
        if (frame.waiting != nullptr)
            for (int stone : *frame.waiting)
                if (stone != frame.falling)
                    inner.push_back(stone);
        inner.insert(inner.end(), session.drum.begin(), session.drum.end());
        static constexpr double inside[3][2] = {{-.35, -.05}, {.15, .02}, {.42, -.12}};
        for (std::size_t i = 0; i < inner.size() && i < 3; ++i) {
            const int stone = inner[i];
            const stones::StonePicture* picture = resting(stone, session.turn[static_cast<std::size_t>(stone)]);
            if (picture == nullptr)
                continue;
            stones::lay_stone_moved(*picture, rest_.data(), width_, height_, width_, drum.x + inside[i][0] * drum.radius,
                                    drum.y + inside[i][1] * drum.radius, .75, static_cast<double>(i) * 1.3 + frame.turn, 1, 1, bgra,
                                    0, 0, width_, height_);
        }
    }
    draw_tumbler(target, drum, TumblerPass::over);
    if (!stones)
        return;
    const Session& session = *frame.session;
    const int count = session.puzzle.n * session.puzzle.n;
    // The cabinet's stones, then the felt's from the back of it to the front, then the pile.
    std::vector<std::pair<double, int>> order;
    for (int stone = 0; stone < count; ++stone) {
        const Where& w = session.where[static_cast<std::size_t>(stone)];
        if (w.place == Place::drum || stone == frame.held || stone == frame.falling ||
            (frame.waiting != nullptr && std::find((*frame.waiting).begin(), (*frame.waiting).end(), stone) != (*frame.waiting).end()))
            continue;
        double x = 0;
        double y = 0;
        stone_point(layout_, session, stone, x, y);
        const double layer = w.place == Place::cabinet ? 0 : w.place == Place::felt ? 1 : 2;
        order.push_back({layer * 100000 + y, stone});
    }
    std::sort(order.begin(), order.end());
    for (const std::pair<double, int>& item : order) {
        const int stone = item.second;
        const stones::StonePicture* picture = resting(stone, session.turn[static_cast<std::size_t>(stone)]);
        if (picture == nullptr)
            continue;
        double x = 0;
        double y = 0;
        stone_point(layout_, session, stone, x, y);
        stones::lay_stone(*picture, rest_.data(), width_, height_, width_, x * s, y * s, bgra, 0, 0, width_, height_);
    }
    // The clues' brass pins.
    const render::Rect all{0, 0, width_, height_};
    for (int cell = 0; cell < count; ++cell) {
        if (!session.puzzle.clue[static_cast<std::size_t>(cell)])
            continue;
        const Box b = cell_box(layout_, cell);
        const double r = std::max(2.5, layout_.cell * .04) * s;
        const double x = (b.x + b.w * .82) * s;
        const double y = (b.y + b.h * .18) * s;
        render::r2d::rounded(target, all, x - r, y - r, 2 * r, 2 * r, r, colour(render::r2d::hex(0x7a5a24)), 1.0F,
                             colour(render::r2d::hex(0x3a2810)), .6 * s);
        render::r2d::rounded(target, all, x - r * .55, y - r * .65, r * .8, r * .8, r * .4, colour(render::r2d::hex(0xf2d28a)), .9F,
                             colour(ink), 0);
    }
}

void Scene::draw(const render::Target& target, const Frame& frame) {
    if (table_.empty() || target.width != width_ || target.height != height_) {
        render::r2d::fill(target, render::Rect{0, 0, target.width, target.height}, colour(render::r2d::hex(0x24170e)));
        return;
    }
    const std::uint64_t signature = rest_signature(frame);
    if (!rest_valid_ || signature != rest_key_)
        compose_rest(frame, signature);
    for (int y = 0; y < height_; ++y)
        std::memcpy(target.pixels + static_cast<std::ptrdiff_t>(y) * target.stride,
                    rest_.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width_),
                    static_cast<std::size_t>(width_) * 4);
    const double s = scale_;
    const bool bgra = target.order == render::Order::bgra;
    draw_marks(target, frame);
    if (showing(frame)) {
        const Session& session = *frame.session;
        // A stone on its way from the tumbler's mouth to the pile: out over the lip, then down.
        if (frame.falling >= 0) {
            const stones::StonePicture* picture = resting(frame.falling, session.turn[static_cast<std::size_t>(frame.falling)]);
            if (picture != nullptr) {
                double tx = 0;
                double ty = 0;
                stone_point(layout_, session, frame.falling, tx, ty);
                const double u = std::clamp(frame.fall, 0.0, 1.0);
                const double x = layout_.mouth_x + (tx - layout_.mouth_x) * u;
                const double y = layout_.mouth_y + (ty - layout_.mouth_y) * u * u;
                stones::lay_stone_moved(*picture, target.pixels, target.width, target.height, target.stride, x * s, y * s,
                                        .8 + .2 * u, frame.turn * (1 - u), 1, 1, bgra, 0, 0, target.width, target.height);
            }
        }
        // The stone in hand, lifted.
        if (frame.held >= 0) {
            const int orientation = session.turn[static_cast<std::size_t>(frame.held)];
            const stones::StonePicture* picture = lifted(frame.held, orientation);
            if (picture == nullptr)
                picture = resting(frame.held, orientation);
            if (picture != nullptr)
                stones::lay_stone(*picture, target.pixels, target.width, target.height, target.stride, frame.held_x * s,
                                  frame.held_y * s, bgra, 0, 0, target.width, target.height);
        }
        draw_counts(target, frame);
    }
    if (frame.panel != Panel::none)
        draw_panel(target, frame);
    if (frame.help)
        draw_help(target);
}

// The socket the keyboard is on, the one a stone in hand would go to, and the clashes of
// a full cabinet.
void Scene::draw_marks(const render::Target& target, const Frame& frame) const {
    const double s = scale_;
    const render::Rect all{0, 0, target.width, target.height};
    const render::Color none{0, 0, 0, 0};
    if (frame.clashes && showing(frame)) {
        for (int cell : clashes(*frame.session)) {
            const Box b = cell_box(layout_, cell);
            const double inset = b.w * .05;
            render::r2d::rounded(target, all, (b.x + inset) * s, (b.y + inset) * s, (b.w - 2 * inset) * s, (b.h - 2 * inset) * s,
                                 b.w * .2 * s, colour(render::r2d::hex(0xc0392b)), .16F, colour(render::r2d::hex(0xe0604a)),
                                 std::max(1.5, 2.2 * s));
        }
    }
    const int marked[2] = {frame.cursor, frame.target};
    for (int k = 0; k < 2; ++k) {
        const int cell = marked[k];
        if (cell < 0 || cell >= layout_.n * layout_.n)
            continue;
        const Box b = cell_box(layout_, cell);
        const double inset = b.w * .04;
        const Col edge = k == 1 && !frame.target_ok ? render::r2d::hex(0xc0392b) : gold;
        render::r2d::rounded(target, all, (b.x + inset) * s, (b.y + inset) * s, (b.w - 2 * inset) * s, (b.h - 2 * inset) * s,
                             b.w * .2 * s, none, 0.0F, colour(edge, .9F), std::max(1.5, 2.0 * s));
    }
}

// The moves and the time, at the foot of the felt.
void Scene::draw_counts(const render::Target& target, const Frame& frame) const {
    const Session& session = *frame.session;
    const double s = scale_;
    const render::Rect all{0, 0, target.width, target.height};
    const double size = layout_.type * s;
    const std::string line = std::to_string(session.moves) + (session.moves == 1 ? " move   " : " moves   ") +
                             clock_text(session.seconds);
    const Mask& mask = text_mask(line, Font::speech, size, 0);
    const double y = (layout_.felt.y + layout_.felt.h) * s - mask.h - 6 * s;
    put_text(target, all, line, Font::speech, size, (layout_.felt.x + 10) * s, y, cream, 0);
}

void Scene::draw_panel(const render::Target& target, const Frame& frame) const {
    const double s = scale_;
    const render::Rect clip{0, 0, target.width, target.height};
    render::r2d::tint(target, clip, colour(ink), .42F);
    // The card fits what it holds: the sizes and Back, the sizes alone, or the tally and
    // its button.
    Box p = layout_.panel;
    if (frame.panel == Panel::won)
        p.h = layout_.again.y + layout_.again.h + 20 - p.y;
    else if (!frame.can_cancel)
        p.h = layout_.choose[0].y + layout_.choose[0].h + 22 - p.y;
    else
        p.h = layout_.cancel.y + layout_.cancel.h + 18 - p.y;
    render::r2d::rounded(target, clip, p.x * s, p.y * s, p.w * s, p.h * s, 14 * s, colour(render::r2d::hex(0x1d130c)), .96F,
                         colour(render::r2d::hex(0x9c7a3c)), 1.6 * s);
    const double size = layout_.type * s;
    const double cx = (p.x + p.w * .5) * s;
    const double wide = (p.w - 40) * s;
    double y = (p.y + 14) * s;
    const Session* session = frame.session;
    if (frame.panel == Panel::choose) {
        y += put_text(target, clip, "A new cabinet", Font::title, layout_.title * s, cx, y, cream, 1, wide) + size * .3;
        put_text(target, clip, "No row or column may repeat a shape or a material.", Font::speech, size * .9, cx, y, quiet, 1,
                 wide);
        for (std::size_t k = 0; k < sizes.size(); ++k) {
            const Box& b = layout_.choose[k];
            const int n = sizes[k];
            const bool chosen = frame.pressed == static_cast<int>(k);
            render::r2d::rounded(target, clip, b.x * s, b.y * s, b.w * s, b.h * s, 10 * s,
                                 colour(chosen ? render::r2d::hex(0x7a5a24) : render::r2d::hex(0xa8823e)), 1.0F,
                                 colour(render::r2d::hex(0x3a2810)), 1.2 * s);
            const std::string title = std::to_string(n) + " \xc3\x97 " + std::to_string(n);
            double ty = (b.y + 8) * s;
            ty += put_text(target, clip, title, Font::title, size * 1.4, (b.x + b.w * .5) * s, ty, render::r2d::hex(0x1c120a), 1);
            ty += put_text(target, clip, std::to_string(n * n) + " stones", Font::speech, size * .8, (b.x + b.w * .5) * s, ty,
                           render::r2d::hex(0x2a1a0c), 1);
            std::string best = "not yet solved";
            if (session != nullptr) {
                const Best& record = (*session).best[static_cast<std::size_t>(n)];
                if (record.moves > 0)
                    best = "best " + std::to_string(record.moves) + ", " + clock_text(record.seconds);
            }
            put_text(target, clip, best, Font::speech, size * .72, (b.x + b.w * .5) * s, ty + size * .15, render::r2d::hex(0x3a2810), 1,
                     (b.w - 8) * s);
        }
        if (frame.can_cancel) {
            const Box& b = layout_.cancel;
            render::r2d::rounded(target, clip, b.x * s, b.y * s, b.w * s, b.h * s, 8 * s,
                                 colour(frame.pressed == 3 ? render::r2d::hex(0x5a4020) : render::r2d::hex(0x3a2a18)), 1.0F,
                                 colour(render::r2d::hex(0x9c7a3c)), 1.0 * s);
            const Mask& m = text_mask("Back", Font::speech, size, 0);
            put_text(target, clip, "Back", Font::speech, size, (b.x + b.w * .5) * s, (b.y + b.h * .5) * s - m.h * .5, cream, 1);
        }
        return;
    }
    // Solved.
    y += size * .3;
    y += put_text(target, clip, "The cabinet is complete", Font::title, layout_.title * s, cx, y, gold, 1, wide) + size * .6;
    if (session != nullptr) {
        const Session& sn = *session;
        const std::string tally = std::to_string(sn.moves) + " moves in " + clock_text(sn.seconds);
        y += put_text(target, clip, tally, Font::speech, size * 1.1, cx, y, cream, 1, wide) + size * .5;
        const Best& record = sn.best[static_cast<std::size_t>(sn.puzzle.n)];
        const std::string note = frame.new_best ? "A new best for this size."
                                                : "Best: " + std::to_string(record.moves) + " moves, " + clock_text(record.seconds);
        put_text(target, clip, note, Font::speech, size * .9, cx, y, quiet, 1, wide);
    }
    const Box& b = layout_.again;
    render::r2d::rounded(target, clip, b.x * s, b.y * s, b.w * s, b.h * s, 9 * s,
                         colour(frame.pressed == 4 ? render::r2d::hex(0x7a5a24) : render::r2d::hex(0xa8823e)), 1.0F,
                         colour(render::r2d::hex(0x3a2810)), 1.2 * s);
    const Mask& m = text_mask("Another cabinet", Font::title, size, 0);
    put_text(target, clip, "Another cabinet", Font::title, size, (b.x + b.w * .5) * s, (b.y + b.h * .5) * s - m.h * .5,
             render::r2d::hex(0x1c120a), 1);
}

void Scene::draw_help(const render::Target& target) const {
    const double s = scale_;
    const render::Rect clip{0, 0, target.width, target.height};
    render::r2d::tint(target, clip, colour(ink), .55F);
    const double w = std::min(layout_.width - 32, 560.0);
    const double h = std::min(layout_.height - 24, 520.0);
    const double x = (layout_.width - w) * .5;
    const double y = (layout_.height - h) * .5;
    render::r2d::rounded(target, clip, x * s, y * s, w * s, h * s, 14 * s, colour(render::r2d::hex(0x1d130c)), .97F,
                         colour(render::r2d::hex(0x9c7a3c)), 1.6 * s);
    const double size = std::clamp(h * .034, 10.5, 15.0) * s;
    double top = (y + 16) * s;
    top += put_text(target, clip, "Specimen Cabinet", Font::title, size * 1.5, (x + 20) * s, top, gold, 0) + size * .5;
    const char* const paragraphs[] = {
        "Every stone is one of a few shapes in one of a few materials, each pairing once. Fill the cabinet so that no row "
        "and no column holds two stones of one shape or of one material.",
        "The tumbler drops stones onto the pile at the head of the felt. Drag a stone, or click it and click where it goes: "
        "into a socket, or anywhere on the felt to sort as you like. A stone set on a filled socket trades places with the "
        "one there. Pinned stones are clues and stay.",
        "When the cabinet is full, any clash is marked. Moves are stones set in sockets; the felt is free.",
        "Keys: arrows move among the sockets, Enter takes or sets the stone there, Space takes the next stone from the pile, "
        "Escape puts a stone back, N for a new cabinet. H or Escape closes this card."};
    for (const char* paragraph : paragraphs)
        top += put_text(target, clip, paragraph, Font::speech, size, (x + 20) * s, top, cream, 0, (w - 40) * s) + size * .7;
}

}  // namespace sc
