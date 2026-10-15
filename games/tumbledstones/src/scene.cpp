#include "scene.hpp"

#include "noise.hpp"
#include "platform/text.hpp"
#include "r2d.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ts {

struct Scene::Art {
    std::uint32_t family_seed = 0;
    ShapeEntry* shape = nullptr;
    Finish finish;
    MineralPicture picture;
    bool ready = false;
};

struct Scene::PictureEntry {
    const Art* art = nullptr;
    int orientation = 0;
    double lift = 0;
    bool shine = false;
    StonePicture picture;
    bool ready = false;
};

namespace {

using render::r2d::Col;

const Col cream = render::r2d::hex(0xeee2c8);
const Col quiet = render::r2d::hex(0xb8a688);
const Col gold = render::r2d::hex(0xe2b35a);
const Col ink = render::r2d::hex(0x0d0805);
constexpr int picture_side = 112;  // a mineral's texels across a face

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

bool lower_first(const Piece* a, const Piece* b) {
    return (*a).lift < (*b).lift;
}

struct PrintJob {
    const Shape* shape;
    const Finish* finish;
    MineralPicture* picture;
};
struct PrintRound {
    std::vector<PrintJob> jobs;
};
void print_one(void* context, int index) noexcept {
    PrintRound& round = *static_cast<PrintRound*>(context);
    PrintJob& job = round.jobs[static_cast<std::size_t>(index)];
    try {
        print_mineral(*job.finish, *job.shape, picture_side, *job.picture);
    } catch (...) {
        (*job.picture).side = 0;
    }
}

struct DrawJob {
    StoneLook look;
    int orientation = 0;
    double lift = 0;
    StonePicture* out = nullptr;
    bool shine = false;
};
struct DrawRound {
    std::vector<DrawJob> jobs;
    double radius = 30;
};
void draw_one(void* context, int index) noexcept {
    DrawRound& round = *static_cast<DrawRound*>(context);
    DrawJob& job = round.jobs[static_cast<std::size_t>(index)];
    try {
        StoneWork work;
        DrawStone how;
        how.radius = round.radius;
        how.lift = job.lift;
        how.fineness = 2;
        how.shine = job.shine;
        draw_stone(job.look, orientation_pose(job.orientation), how, work, *job.out);
    } catch (...) {
        (*job.out) = StonePicture{};
    }
}

struct TrayJob {
    const TrayPlan* plan;
    std::uint32_t* pixels;
    bool bgra;
    int bands;
};
void tray_band(void* context, int index) noexcept {
    const TrayJob& job = *static_cast<const TrayJob*>(context);
    const int rows = (*job.plan).height;
    const int row0 = rows * index / job.bands;
    const int row1 = rows * (index + 1) / job.bands;
    draw_tray(*job.plan, job.pixels, (*job.plan).width, job.bgra, row0, row1);
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

}  // namespace

std::string number_word(int n, bool capital) {
    static const char* const words[] = {"zero", "one", "two", "three", "four", "five", "six",
                                        "seven", "eight", "nine", "ten", "eleven", "twelve", "thirteen",
                                        "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen", "twenty"};
    std::string word = n >= 0 && n <= 20 ? words[n] : std::to_string(n);
    if (capital && !word.empty() && word[0] >= 'a' && word[0] <= 'z')
        word[0] = static_cast<char>(word[0] - 'a' + 'A');
    return word;
}

Scene::Scene() = default;
Scene::~Scene() = default;

void Scene::set_runner(Runner runner) {
    runner_ = runner;
}

double Scene::radius() const {
    return layout_.cell * .42 * scale_;
}

std::uint64_t Scene::look_key(std::uint32_t serial, int orientation, std::uint32_t family) {
    return (static_cast<std::uint64_t>(serial) << 32) ^ (static_cast<std::uint64_t>(family) * 8) ^
           static_cast<std::uint64_t>(orientation);
}

void Scene::resize(const Layout& layout, double scale, render::Order order) {
    const int width = std::max(1, static_cast<int>(std::lround(layout.width * scale)));
    const int height = std::max(1, static_cast<int>(std::lround(layout.height * scale)));
    const bool same_stones = layout.cell == layout_.cell && scale == scale_;
    layout_ = layout;
    scale_ = scale;
    order_ = order;
    width_ = width;
    height_ = height;
    TrayPlan plan;
    plan.width = width;
    plan.height = height;
    plan.scale = scale;
    plan.grid_x = layout.grid_x * scale;
    plan.grid_y = layout.grid_y * scale;
    plan.cell = layout.cell * scale;
    plan.wall = std::max(6.0, 9 * scale);
    tray_.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0U);
    TrayJob job{&plan, tray_.data(), order == render::Order::bgra, 16};
    run_jobs(runner_, job.bands, tray_band, &job);
    rest_.assign(tray_.size(), 0U);
    rest_shown_.assign(cell_count, Shown{});
    rest_valid_ = false;
    if (!same_stones) {
        if (batch_open_) {
            batch_stale_ = true;
        } else {
            pictures_.clear();
            lifted_.clear();
            hovered_.clear();
            pending_pictures_.clear();
        }
    }
}

Scene::ShapeEntry& Scene::shape_for(const Family& family) {
    std::unique_ptr<ShapeEntry>& slot = shapes_[family.seed];
    if (!slot) {
        slot = std::make_unique<ShapeEntry>();
        (*slot).family = family;
        pending_shapes_.push_back(slot.get());
    }
    return *slot;
}

Scene::Art& Scene::art_for(std::uint32_t serial, const Stone& look, const Family& family) {
    const std::uint64_t key = (static_cast<std::uint64_t>(serial) << 32) ^ family.seed;
    std::unique_ptr<Art>& slot = art_[key];
    if (!slot) {
        slot = std::make_unique<Art>();
        Art& art = *slot;
        art.family_seed = family.seed;
        art.shape = &shape_for(family);
        // The stone's material, in play by the rules, its pattern this stone's own.
        art.finish = make_finish(static_cast<Mineral>(std::clamp(look.material, 0, mineral_count - 1)), look.material_seed);
        art.finish.seed = hash_bits(look.material_seed * 2654435761U ^ look.mineral);
        pending_arts_.push_back(&art);
    }
    return *slot;
}

Scene::PictureEntry& Scene::picture_for(std::map<std::uint64_t, std::unique_ptr<PictureEntry>>& kept, Art& art,
                                        std::uint32_t serial, int orientation, std::uint32_t family, double lift,
                                        bool shine) {
    std::unique_ptr<PictureEntry>& slot = kept[look_key(serial, orientation, family)];
    if (!slot) {
        slot = std::make_unique<PictureEntry>();
        (*slot).art = &art;
        (*slot).orientation = orientation;
        (*slot).lift = lift;
        (*slot).shine = shine;
        pending_pictures_.push_back(slot.get());
    }
    return *slot;
}

const StonePicture* Scene::resting(std::uint32_t serial, int orientation, std::uint32_t family) const {
    std::map<std::uint64_t, std::unique_ptr<PictureEntry>>::const_iterator found = pictures_.find(look_key(serial, orientation, family));
    if (found == pictures_.end() || !(*(*found).second).ready)
        return nullptr;
    return &(*(*found).second).picture;
}

const StonePicture* Scene::lifted(std::uint32_t serial, int orientation, std::uint32_t family) const {
    std::map<std::uint64_t, std::unique_ptr<PictureEntry>>::const_iterator found = lifted_.find(look_key(serial, orientation, family));
    if (found == lifted_.end() || !(*(*found).second).ready)
        return nullptr;
    return &(*(*found).second).picture;
}

const StonePicture* Scene::hovered(std::uint32_t serial, int orientation, std::uint32_t family) const {
    std::map<std::uint64_t, std::unique_ptr<PictureEntry>>::const_iterator found = hovered_.find(look_key(serial, orientation, family));
    if (found == hovered_.end() || !(*(*found).second).ready)
        return nullptr;
    return &(*(*found).second).picture;
}

// The stone under the pointer that lifts and catches the glint: resting, not in hand, its
// picture made.
std::uint32_t Scene::hovered_serial(const Frame& frame) const {
    if (frame.hover < 0 || frame.session == nullptr || frame.motion == nullptr || (*frame.motion).moving())
        return 0;
    const Board& board = (*frame.session).board;
    const Stone& stone = board.cells[static_cast<std::size_t>(frame.hover)];
    if (stone.family < 0 || stone.serial == frame.held)
        return 0;
    const Family& family = board.families[static_cast<std::size_t>(stone.family)];
    return hovered(stone.serial, stone.orientation, family.seed) != nullptr ? stone.serial : 0;
}

const Scene::Art* Scene::art_ready(std::uint32_t serial, std::uint32_t family) const {
    std::map<std::uint64_t, std::unique_ptr<Art>>::const_iterator found = art_.find((static_cast<std::uint64_t>(serial) << 32) ^ family);
    if (found == art_.end() || !(*(*found).second).ready || !(*(*(*found).second).shape).ready)
        return nullptr;
    return (*found).second.get();
}

void Scene::want(const Frame& frame) {
    if (frame.motion == nullptr || frame.session == nullptr)
        return;
    const Board& board = (*frame.session).board;
    // Every family the board knows, the one waiting too: its shape is ready before it comes.
    for (const Family& family : board.families)
        if (family.used)
            static_cast<void>(shape_for(family));
    if (tray_.empty())
        return;
    for (const Piece& piece : (*frame.motion).pieces()) {
        if (piece.gone)
            continue;
        Art& art = art_for(piece.serial, piece.look, piece.family);
        static_cast<void>(picture_for(pictures_, art, piece.serial, piece.look.orientation, piece.family.seed, 0));
        if (piece.landing_look.orientation != piece.look.orientation || piece.landing_family.seed != piece.family.seed) {
            Art& landing = art_for(piece.serial, piece.landing_look, piece.landing_family);
            static_cast<void>(picture_for(pictures_, landing, piece.serial, piece.landing_look.orientation, piece.landing_family.seed, 0));
        }
    }
    if (frame.held != 0 && frame.held_cell >= 0) {
        const Stone& stone = board.cells[static_cast<std::size_t>(frame.held_cell)];
        const Family& family = board.families[static_cast<std::size_t>(stone.family)];
        Art& art = art_for(stone.serial, stone, family);
        static_cast<void>(picture_for(lifted_, art, stone.serial, frame.held_orientation, family.seed, 1));
    }
    if (frame.drag_cell >= 0) {
        const Stone& stone = board.cells[static_cast<std::size_t>(frame.drag_cell)];
        const Family& family = board.families[static_cast<std::size_t>(stone.family)];
        Art& art = art_for(stone.serial, stone, family);
        static_cast<void>(picture_for(lifted_, art, stone.serial, stone.orientation, family.seed, 1));
    }
    if (frame.hover >= 0) {
        const Stone& stone = board.cells[static_cast<std::size_t>(frame.hover)];
        if (stone.family >= 0 && stone.serial != frame.held) {
            const Family& family = board.families[static_cast<std::size_t>(stone.family)];
            Art& art = art_for(stone.serial, stone, family);
            static_cast<void>(picture_for(hovered_, art, stone.serial, stone.orientation, family.seed, .3, true));
        }
    }
}

bool Scene::begin_batch() {
    if (batch_open_ || (pending_shapes_.empty() && pending_arts_.empty() && pending_pictures_.empty()))
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

namespace {

struct ShapeJob {
    Family family;
    std::unique_ptr<Shape>* out;
};
struct ShapeRound {
    std::vector<ShapeJob> jobs;
};
void make_one_shape(void* context, int index) noexcept {
    ShapeJob& job = (*static_cast<ShapeRound*>(context)).jobs[static_cast<std::size_t>(index)];
    try {
        const Family& family = job.family;
        const Style style = static_cast<Style>(std::clamp(family.style, 0, style_count - 1));
        if (family.parent != 0) {
            // A near relation of another family: drawn towards that family's own measures.
            const Shape parent = Shape::make(family.parent, style);
            *job.out = std::make_unique<Shape>(Shape::make(family.seed, style, family.likeness / 100.0, &parent));
        } else {
            *job.out = std::make_unique<Shape>(Shape::make(family.seed, style));
        }
    } catch (...) {
        *job.out = std::make_unique<Shape>(Shape::make(1, Style::facet));
    }
}

}  // namespace

void Scene::run_batch() noexcept {
    // The families' shapes first, then the minerals on them, then the lit pictures.
    ShapeRound shapes;
    for (ShapeEntry* entry : batch_shapes_)
        shapes.jobs.push_back({(*entry).family, &(*entry).shape});
    run_jobs(runner_, static_cast<int>(shapes.jobs.size()), make_one_shape, &shapes);
    PrintRound prints;
    for (Art* art : batch_arts_)
        prints.jobs.push_back({(*(*art).shape).shape.get(), &(*art).finish, &(*art).picture});
    run_jobs(runner_, static_cast<int>(prints.jobs.size()), print_one, &prints);
    DrawRound draws;
    draws.radius = batch_radius_;
    for (PictureEntry* entry : batch_pictures_) {
        const Art& art = *(*entry).art;
        if ((*art.shape).shape == nullptr || art.picture.side == 0)
            continue;
        draws.jobs.push_back({StoneLook{(*art.shape).shape.get(), &art.finish, &art.picture}, (*entry).orientation,
                              (*entry).lift, &(*entry).picture, (*entry).shine});
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
        batch_stale_ = false;
        pictures_.clear();
        lifted_.clear();
        hovered_.clear();
        pending_pictures_.clear();
    }
}

void Scene::forget_unused(const Session& session) {
    if (batch_open_)
        return;
    std::map<std::uint64_t, bool> arts;
    std::map<std::uint64_t, bool> shown;
    for (const Stone& stone : session.board.cells) {
        if (stone.family < 0)
            continue;
        const Family& family = session.board.families[static_cast<std::size_t>(stone.family)];
        arts[(static_cast<std::uint64_t>(stone.serial) << 32) ^ family.seed] = true;
        shown[look_key(stone.serial, stone.orientation, family.seed)] = true;
    }
    // Pending work for what is gone goes too.
    std::vector<PictureEntry*> pictures_left;
    for (std::map<std::uint64_t, std::unique_ptr<PictureEntry>>::iterator it = pictures_.begin(); it != pictures_.end();) {
        if (shown.count((*it).first) == 0)
            it = pictures_.erase(it);
        else
            ++it;
    }
    lifted_.clear();
    if (hovered_.size() > 12)
        hovered_.clear();
    for (std::map<std::uint64_t, std::unique_ptr<Art>>::iterator it = art_.begin(); it != art_.end();) {
        if (arts.count((*it).first) == 0)
            it = art_.erase(it);
        else
            ++it;
    }
    pending_pictures_.clear();
    for (std::pair<const std::uint64_t, std::unique_ptr<PictureEntry>>& entry : pictures_)
        if (!(*entry.second).ready)
            pending_pictures_.push_back(entry.second.get());
    for (std::map<std::uint64_t, std::unique_ptr<PictureEntry>>::iterator it = hovered_.begin(); it != hovered_.end();) {
        // One still to make may point at art just let go: it is asked for again if wanted.
        if (!(*(*it).second).ready)
            it = hovered_.erase(it);
        else
            ++it;
    }
    pending_arts_.clear();
    for (std::pair<const std::uint64_t, std::unique_ptr<Art>>& entry : art_)
        if (!(*entry.second).ready)
            pending_arts_.push_back(entry.second.get());
    std::map<std::uint32_t, bool> families;
    for (const Family& family : session.board.families)
        if (family.used)
            families[family.seed] = true;
    for (std::map<std::uint32_t, std::unique_ptr<ShapeEntry>>::iterator it = shapes_.begin(); it != shapes_.end();) {
        bool in_use = families.count((*it).first) != 0;
        for (const std::pair<const std::uint64_t, std::unique_ptr<Art>>& art : art_)
            in_use = in_use || (*art.second).shape == (*it).second.get();
        if (!in_use)
            it = shapes_.erase(it);
        else
            ++it;
    }
    pending_shapes_.clear();
    for (std::pair<const std::uint32_t, std::unique_ptr<ShapeEntry>>& entry : shapes_)
        if (!(*entry.second).ready)
            pending_shapes_.push_back(entry.second.get());
}

void Scene::refresh_rest(const Frame& frame) {
    // What each socket should show at rest.
    std::vector<Shown> want(cell_count);
    std::vector<const Piece*> at_rest(cell_count, nullptr);
    std::uint32_t dragged = 0;
    if (frame.drag_cell >= 0)
        dragged = (*frame.session).board.cells[static_cast<std::size_t>(frame.drag_cell)].serial;
    const std::uint32_t lifting = hovered_serial(frame);
    for (const Piece& piece : (*frame.motion).pieces()) {
        if (piece.moving || piece.gone || piece.serial == frame.held || piece.serial == dragged ||
            (lifting != 0 && piece.serial == lifting))
            continue;
        const int column = static_cast<int>(std::floor(piece.x));
        const int row = static_cast<int>(std::floor(piece.y));
        if (column < 0 || row < 0 || column >= side || row >= side)
            continue;
        const int cell = row * side + column;
        want[static_cast<std::size_t>(cell)] = Shown{piece.serial, piece.look.orientation, piece.family.seed, piece.look.swapped};
        at_rest[static_cast<std::size_t>(cell)] = &piece;
    }
    // The cells whose picture changed, and the rectangle they reach.
    const double cell = layout_.cell * scale_;
    const double reach = cell * .95;
    render::Rect dirty;
    for (int c = 0; c < cell_count; ++c) {
        const Shown& a = want[static_cast<std::size_t>(c)];
        const Shown& b = rest_shown_[static_cast<std::size_t>(c)];
        if (rest_valid_ && a.serial == b.serial && a.orientation == b.orientation && a.family == b.family &&
            a.swapped == b.swapped)
            continue;
        double x = 0;
        double y = 0;
        cell_centre(layout_, c, x, y);
        dirty = dirty.united(render::pixel_bounds(x * scale_ - reach, y * scale_ - reach, x * scale_ + reach, y * scale_ + reach));
    }
    if (!rest_valid_)
        dirty = render::Rect{0, 0, width_, height_};
    dirty = dirty.intersected(render::Rect{0, 0, width_, height_});
    rest_shown_ = want;
    rest_valid_ = true;
    if (dirty.empty())
        return;
    for (int y = dirty.y0; y < dirty.y1; ++y) {
        const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(width_);
        std::memcpy(rest_.data() + row + static_cast<std::size_t>(dirty.x0), tray_.data() + row + static_cast<std::size_t>(dirty.x0),
                    static_cast<std::size_t>(dirty.x1 - dirty.x0) * sizeof(std::uint32_t));
    }
    // Lower stones first: the shade of a stone further up falls over the one below.
    std::vector<int> marked;
    for (int c = cell_count - 1; c >= 0; --c) {
        const Piece* piece = at_rest[static_cast<std::size_t>(c)];
        if (piece == nullptr)
            continue;
        if ((*piece).look.swapped)
            marked.push_back(c);
        const StonePicture* picture = resting((*piece).serial, (*piece).look.orientation, (*piece).family.seed);
        if (picture == nullptr)
            continue;
        double x = 0;
        double y = 0;
        cell_centre(layout_, c, x, y);
        lay_stone(*picture, rest_.data(), width_, height_, width_, x * scale_, y * scale_,
                  order_ == render::Order::bgra, dirty.x0, dirty.y0, dirty.x1, dirty.y1);
    }
    const render::Target kept{rest_.data(), width_, width_, height_, order_};
    draw_marks(kept, dirty, marked);
}

// A swapped stone's mark: a small brass pin in its socket's top-left corner.
void Scene::draw_marks(const render::Target& target, render::Rect clip, const std::vector<int>& cells) const {
    const double cell = layout_.cell * scale_;
    const double r = std::max(2.5, cell * .05);
    for (int c : cells) {
        const double x = (layout_.grid_x + (c % side) * layout_.cell) * scale_ + cell * .1;
        const double y = (layout_.grid_y + (c / side) * layout_.cell) * scale_ + cell * .1;
        render::r2d::rounded(target, clip, x - r, y - r, 2 * r, 2 * r, r, colour(render::r2d::hex(0x6a4a1c)), 1.0F,
                             colour(render::r2d::hex(0x2a1a0a)), std::max(1.0, scale_ * .6));
        const double hi = r * .5;
        render::r2d::rounded(target, clip, x - hi * 1.1, y - hi * 1.2, hi * 1.3, hi * 1.3, hi * .65,
                             colour(render::r2d::hex(0xf2d08a)), .9F, colour(ink), 0);
    }
}

void Scene::draw(const render::Target& target, const Frame& frame) {
    if (tray_.empty() || frame.session == nullptr || frame.motion == nullptr)
        return;
    refresh_rest(frame);
    const render::Rect all{0, 0, std::min(width_, target.width), std::min(height_, target.height)};
    for (int y = 0; y < all.y1; ++y)
        std::memcpy(target.row(y), rest_.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width_),
                    static_cast<std::size_t>(all.x1) * sizeof(std::uint32_t));
    const bool bgra = target.order == render::Order::bgra;
    const double cell = layout_.cell * scale_;
    const Board& board = (*frame.session).board;
    // The socket under the pointer, or the keyboard's: a warm edge round it.
    const int lit[2] = {frame.hover, frame.cursor};
    for (int k = 0; k < 2; ++k) {
        const int c = lit[k];
        if (c < 0 || frame.panel != Panel::none)
            continue;
        const double x = (layout_.grid_x + (c % side) * layout_.cell) * scale_;
        const double y = (layout_.grid_y + (c / side) * layout_.cell) * scale_;
        const double inset = cell * .04;
        render::r2d::rounded(target, all, x + inset, y + inset, cell - 2 * inset, cell - 2 * inset, cell * .2,
                             colour(gold), 0.0F, colour(gold, k == 0 ? .55F : .9F), std::max(1.5, scale_ * 1.4));
    }
    if (frame.drag_to >= 0) {
        const int c = frame.drag_to;
        const double x = (layout_.grid_x + (c % side) * layout_.cell) * scale_;
        const double y = (layout_.grid_y + (c / side) * layout_.cell) * scale_;
        const Col edge = frame.drag_allowed ? render::r2d::hex(0x9ad08a) : render::r2d::hex(0xd07a6a);
        render::r2d::rounded(target, all, x + cell * .04, y + cell * .04, cell * .92, cell * .92, cell * .2, colour(edge),
                             .08F, colour(edge, .9F), std::max(1.5, scale_ * 1.6));
    }
    // Stones in motion, the lowest first.
    std::vector<const Piece*> moving;
    for (const Piece& piece : (*frame.motion).pieces())
        if (piece.moving && !piece.gone)
            moving.push_back(&piece);
    std::stable_sort(moving.begin(), moving.end(), lower_first);
    for (const Piece* piece : moving) {
        const StonePicture* picture = resting((*piece).serial, (*piece).look.orientation, (*piece).family.seed);
        if (picture == nullptr)
            continue;
        const double x = (layout_.grid_x + (*piece).x * layout_.cell) * scale_;
        const double y = (layout_.grid_y + (*piece).y * layout_.cell) * scale_;
        lay_stone_moved(*picture, target.pixels, target.width, target.height, target.stride, x, y,
                        (*piece).scale, (*piece).spin, (*piece).squash, (*piece).opacity, bgra, 0, 0, target.width,
                        target.height);
    }
    // The stone under the pointer: lifted a touch, rocking as it settles, and the pointer's
    // light glinting on its facets.
    const std::uint32_t lifting = hovered_serial(frame);
    if (lifting != 0) {
        const Stone& stone = board.cells[static_cast<std::size_t>(frame.hover)];
        const Family& family = board.families[static_cast<std::size_t>(stone.family)];
        const StonePicture* picture = hovered(stone.serial, stone.orientation, family.seed);
        double x = 0;
        double y = 0;
        cell_centre(layout_, frame.hover, x, y);
        const double cx = x * scale_;
        const double cy = (y - frame.hover_lift * layout_.cell * .02) * scale_;
        lay_stone_moved(*picture, target.pixels, target.width, target.height, target.stride, cx, cy,
                        1 + .02 * frame.hover_lift, frame.hover_spin, 1, 1, bgra, 0, 0, target.width, target.height);
        lay_glint(*picture, target.pixels, target.width, target.height, target.stride, cx, cy, frame.hover_x * scale_,
                  frame.hover_y * scale_, layout_.cell * scale_ * 1.1, .9, bgra);
    }
    // The stone in hand.
    if (frame.held != 0 && frame.held_cell >= 0) {
        const Stone& stone = board.cells[static_cast<std::size_t>(frame.held_cell)];
        const Family& family = board.families[static_cast<std::size_t>(stone.family)];
        double x = 0;
        double y = 0;
        cell_centre(layout_, frame.held_cell, x, y);
        const Art* art = art_ready(stone.serial, family.seed);
        const StonePicture* still = lifted(stone.serial, frame.held_orientation, family.seed);
        if ((frame.held_turning || still == nullptr) && art != nullptr) {
            // Turning, or its lifted picture still being made: drawn here and now, plainly.
            const Art& a = *art;
            DrawStone how;
            how.radius = radius();
            how.lift = 1;
            how.fineness = 1;
            draw_stone(StoneLook{(*a.shape).shape.get(), &a.finish, &a.picture}, frame.held_pose, how, work_, turning_);
            lay_stone(turning_, target.pixels, target.width, target.height, target.stride, x * scale_, y * scale_, bgra, 0,
                      0, target.width, target.height);
        } else if (still != nullptr) {
            lay_stone(*still, target.pixels, target.width, target.height, target.stride, x * scale_, y * scale_, bgra, 0,
                      0, target.width, target.height);
        }
    }
    if (frame.drag_cell >= 0) {
        const Stone& stone = board.cells[static_cast<std::size_t>(frame.drag_cell)];
        const Family& family = board.families[static_cast<std::size_t>(stone.family)];
        const StonePicture* picture = lifted(stone.serial, stone.orientation, family.seed);
        if (picture == nullptr)
            picture = resting(stone.serial, stone.orientation, family.seed);
        if (picture != nullptr)
            lay_stone(*picture, target.pixels, target.width, target.height, target.stride,
                      frame.drag_x * scale_, frame.drag_y * scale_, bgra, 0, 0, target.width, target.height);
    }
    draw_counts(target, frame);
    if (frame.panel != Panel::none)
        draw_panel(target, frame);
    if (frame.help)
        draw_help(target);
    text_cache_trim();
}

void Scene::draw_counts(const render::Target& target, const Frame& frame) const {
    const Session& session = *frame.session;
    const Board& board = session.board;
    const Box& box = layout_.counts;
    const double s = scale_;
    const render::Rect clip{0, 0, target.width, target.height};
    const double size = layout_.text * s;
    const double x = box.x * s;
    double y = box.y * s;
    const double wide = box.w * s;
    if (!session.playing && frame.panel == Panel::choose)
        return;
    const std::string aim = "A pull of " + number_word(board.target, false);
    y += put_text(target, clip, aim, Font::title, size * 1.45, x, y, cream, 0, wide) + size * .7;
    const std::string lines[4][2] = {{"Moves", std::to_string(board.moves)},
                                     {"Points", std::to_string(board.points)},
                                     {"Total", std::to_string(board.moves + board.points)},
                                     {"Largest pull", board.largest > 0 ? std::to_string(board.largest) : "none yet"}};
    const double right = x + std::min(wide, size * 13);
    for (int i = 0; i < 4; ++i) {
        const Font font = i == 2 ? Font::speech_bold : Font::speech;
        const int h = put_text(target, clip, lines[i][0], font, size, x, y, i == 2 ? cream : quiet, 0);
        put_text(target, clip, lines[i][1], font, size, right, y, cream, 2);
        y += h + size * .35;
    }
    y += size * .5;
    std::string best;
    if (board.target < least_recorded) {
        best = "Pulls under seven keep no best.";
    } else {
        const Best& b = session.best[static_cast<std::size_t>(board.target)];
        best = b.total > 0 ? "Best for " + number_word(board.target, false) + ": " + std::to_string(b.total)
                           : "No best for " + number_word(board.target, false) + " yet.";
    }
    y += put_text(target, clip, best, Font::speech, size * .9, x, y, gold, 0, wide) + size * .9;
    // How to play, quietly, at the foot of the column.
    const std::string hint = "Click a stone to turn it over, right-click to turn it round, drag it to swap. "
                             "Move away to set it down.";
    const double hint_size = size * .78;
    const Mask& mask = text_mask(hint, Font::speech, hint_size, wide);
    const double foot = (box.y + box.h) * s - mask.h;
    if (foot > y)
        put_text(target, clip, hint, Font::speech, hint_size, x, foot, quiet, 0, wide);
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
    const double size = std::clamp(h * .036, 10.5, 15.0) * s;
    double top = (y + 16) * s;
    top += put_text(target, clip, "Tumbled Stones", Font::title, size * 1.5, (x + 20) * s, top, gold, 0) + size * .5;
    const char* const paragraphs[] = {
        "Every stone is one of a few shapes, cut from any mineral. Click a stone to pick it up and turn it over; "
        "right-click or R turns it a quarter round. Move away, or press Space, to set it down. If it then lies exactly "
        "as a neighbour of its shape lies, the joined group is pulled from the tray.",
        "Drag a stone onto a neighbour to swap them: each may swap once until the next juggle. Juggle (J) when "
        "you are stuck; the tray juggles itself when no match is left.",
        "You chose a pull at the start. The game ends when one pull takes that many stones. A larger pull costs "
        "fewer points; your total is moves plus points, and the lowest is the best.",
        "Keys: arrows move, F turns over, R turns round, Space sets down, Shift and an arrow swaps, J juggles, "
        "N starts a new game, Escape puts a stone back. H or Escape closes this card."};
    for (const char* paragraph : paragraphs)
        top += put_text(target, clip, paragraph, Font::speech, size, (x + 20) * s, top, cream, 0, (w - 40) * s) + size * .7;
}

void Scene::draw_panel(const render::Target& target, const Frame& frame) const {
    const Session& session = *frame.session;
    const double s = scale_;
    const render::Rect clip{0, 0, target.width, target.height};
    render::r2d::tint(target, clip, colour(ink), .42F);
    const Box& p = layout_.panel;
    render::r2d::rounded(target, clip, p.x * s, p.y * s, p.w * s, p.h * s, 14 * s, colour(render::r2d::hex(0x1d130c)),
                         .96F, colour(render::r2d::hex(0x9c7a3c)), 1.6 * s);
    const double size = std::clamp(std::min(p.h * .075, p.w * .046), 11.0, 19.0) * s;
    const double cx = (p.x + p.w * .5) * s;
    const double text_wide = (p.w - 88) * s;
    // A button: a rounded brass plate with its word.
    struct Button {
        const Box* box;
        std::string label;
        int id;
    };
    std::vector<Button> buttons;
    double y = (p.y + 12) * s;
    if (frame.panel == Panel::choose) {
        y += put_text(target, clip, "How many in one pull?", Font::title, size * 1.25, cx, y, cream, 1, text_wide) + size * .3;
        put_text(target, clip, "Play until a single pull takes that many stones.", Font::speech, size * .82, cx, y, quiet, 1,
                 (p.w - 40) * s);
        const Mask& number = text_mask(std::to_string(frame.choosing), Font::title, size * 3.0, 0);
        const double middle = (layout_.fewer.y + layout_.fewer.h * .5) * s;
        put_text(target, clip, std::to_string(frame.choosing), Font::title, size * 3.0, cx, middle - number.h * .5, gold, 1);
        std::string note;
        if (frame.choosing < least_recorded) {
            note = frame.choosing == 2 ? "Two is won with the first pull." : "Pulls under seven keep no best.";
        } else {
            const Best& b = session.best[static_cast<std::size_t>(frame.choosing)];
            note = b.total > 0 ? "Best: " + std::to_string(b.total) + " (" + std::to_string(b.moves) + " moves, " +
                                     std::to_string(b.points) + " points)"
                               : "No best yet: the fewest moves plus points wins it.";
        }
        const Mask& note_mask = text_mask(note, Font::speech, size * .78, (p.w - 40) * s);
        const double note_y = layout_.begin.y * s - note_mask.h - size * .5;
        put_text(target, clip, note, Font::speech, size * .78, cx, note_y, quiet, 1, (p.w - 40) * s);
        buttons.push_back({&layout_.fewer, "\xe2\x88\x92", 1});
        buttons.push_back({&layout_.more, "+", 2});
        buttons.push_back({&layout_.begin, "Begin", 3});
        if (frame.can_cancel)
            buttons.push_back({&layout_.cancel, "\xc3\x97", 4});
    } else {
        const Board& board = session.board;
        const std::string title = number_word(board.largest, true) + " in one pull!";
        y += size * .4;
        y += put_text(target, clip, title, Font::title, size * 1.5, cx, y, gold, 1, text_wide) + size * .8;
        const std::string tally = std::to_string(board.moves) + " moves and " + std::to_string(board.points) +
                                  " points: " + std::to_string(board.moves + board.points);
        y += put_text(target, clip, tally, Font::speech, size, cx, y, cream, 1, (p.w - 40) * s) + size * .6;
        std::string note;
        if (board.target < least_recorded)
            note = "Pulls under seven keep no best.";
        else if (frame.new_best)
            note = "A new best for " + number_word(board.target, false) + ".";
        else
            note = "Best for " + number_word(board.target, false) + ": " +
                   std::to_string(session.best[static_cast<std::size_t>(board.target)].total) + ".";
        put_text(target, clip, note, Font::speech, size * .9, cx, y, quiet, 1, (p.w - 40) * s);
        buttons.push_back({&layout_.begin, "Next pull", 3});
    }
    for (const Button& button : buttons) {
        const Box& b = *button.box;
        const bool pressed = frame.pressed_button == button.id;
        const Col face = pressed ? render::r2d::hex(0x7a5a24) : render::r2d::hex(0xa8823e);
        render::r2d::rounded(target, clip, b.x * s, b.y * s, b.w * s, b.h * s, std::min(b.h * .3, 10.0) * s, colour(face),
                             1.0F, colour(render::r2d::hex(0x3a2810)), 1.2 * s);
        const double label_size = (button.id == 1 || button.id == 2 ? b.h * .62 : b.h * .42) * s;
        const Mask& mask = text_mask(button.label, Font::title, label_size, 0);
        put_text(target, clip, button.label, Font::title, label_size, (b.x + b.w * .5) * s,
                 (b.y + b.h * .5) * s - mask.h * .5, render::r2d::hex(0x1c120a), 1);
    }
}

}  // namespace ts
