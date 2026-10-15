#include "business_card.hpp"

#include "cards/card_print.hpp"
#include "help_book.hpp"
#include "help_content.hpp"
#include "open_link.hpp"
#include "paper.hpp"
#include "runtime_paths.hpp"
#include "suite_settings.hpp"

#include "gui_forms/window.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>

namespace games {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr std::string_view kCompany = "RAINSTAR SOFTWARE";
// The card's proportions: three and a half inches by two, and a duplexed stock of 32 points,
// two plies pressed together, as thick as a ninetieth of its width.
constexpr double kAspect = 2.0 / 3.5;
constexpr double kThickness = 1.0 / 90;
// Its lettering, as fractions of the card's height: the company in spaced capitals, the
// address beneath it.
constexpr double kCompanySize = .10;
constexpr double kCompanyTracking = .18;  // of the letters' size, between letters
constexpr double kCompanySpace = .55;     // a word space, of the letters' size
constexpr double kCompanyMiddle = .44;
constexpr double kAddressSize = .068;
constexpr double kAddressMiddle = .70;
// The stock, made by the paper engine as the card table's is, and the ink printed on it.
struct Stock {
    double colour[3];  // sRGB 0..1, as the paper engine takes it
    paper::Preset preset;
    double ink[3];     // linear light
};
// Bone, raised black ink.
constexpr Stock kStock{{233 / 255.0, 227 / 255.0, 210 / 255.0}, paper::Preset::copy, {.010, .009, .008}};
constexpr double kInkHeight = .20;    // points the thermographed ink stands above the stock
constexpr double kFinishSlope = .035; // the finish's dimples at the table's middle depth: their slopes' spread
// How it is held: on the page, lifted a little, more under the pointer; in the hand, at the page.
constexpr double kLiftResting = 10, kLiftHovered = 15;
// Remarks: how many at once and how often.
constexpr int kRemarksAtOnce = 3;
constexpr double kFirstRemark = .45, kRemarkGapMin = 1.3, kRemarkGapSpread = 1.2;
const gf::FontSpec kRemarkFont{gf::FontRole::content, 16, 400, true};
const gf::FontSpec kHintFont{gf::FontRole::content, 13, 400, false};
constexpr std::string_view kHint = "Drag to turn it.   Rainstar Software opens the website.   Escape puts it away.";

double linear_of(double srgb) {
    const double v = srgb <= .04045 ? srgb / 12.92 : std::pow((srgb + .055) / 1.055, 2.4);
    return v;
}
double approach(double value, double target, double rate, double dt) {
    const double moved = target + (value - target) * std::exp(-rate * dt);
    return moved;
}
std::uint32_t scramble(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
double unit_random(std::uint32_t& state) {
    const double u = (scramble(state) & 0xffffffU) / 16777216.0;
    return u;
}
bool overlaps(const gf::Rect& a, const gf::Rect& b) {
    const bool apart = a.x + a.width <= b.x || b.x + b.width <= a.x || a.y + a.height <= b.y || b.y + b.height <= a.y;
    return !apart;
}
gf::Rect inflated(const gf::Rect& r, double by) {
    const gf::Rect grown{r.x - by, r.y - by, r.width + 2 * by, r.height + 2 * by};
    return grown;
}

// Ink laid into a face from a shaped mask whose logical top-left is at (`left`, `top`) points:
// coverage into `ink`, and into `foil` too when the lettering turns to gold.
void lay_ink(const TextImage& text, double left, double top, double density, int width, int height,
             std::vector<float>& ink, std::vector<float>& foil, bool gilded) {
    if (!text.mask.has_value())
        return;
    const gui_forms::TextMaskMetrics m = text.mask.metrics();
    const std::span<const std::uint8_t> coverage = text.mask.coverage();
    if (m.width_px == 0 || m.height_px == 0 || m.stride_bytes < m.width_px ||
        coverage.size() < static_cast<std::size_t>(m.stride_bytes) * m.height_px)
        return;
    const int x0 = static_cast<int>(std::lround(left * density + m.ink_left_px));
    const int y0 = static_cast<int>(std::lround(top * density + m.ink_top_px));
    for (std::uint32_t row = 0; row < m.height_px; ++row) {
        const int y = y0 + static_cast<int>(row);
        if (y < 0 || y >= height)
            continue;
        for (std::uint32_t column = 0; column < m.width_px; ++column) {
            const int x = x0 + static_cast<int>(column);
            if (x < 0 || x >= width)
                continue;
            const float c = coverage[static_cast<std::size_t>(row) * m.stride_bytes + column] / 255.0F;
            const std::size_t i = static_cast<std::size_t>(y) * width + x;
            ink[i] = std::max(ink[i], c);
            if (gilded)
                foil[i] = std::max(foil[i], c > 0 ? 1.0F : 0.0F);
        }
    }
}

// A box blur of `radius` texels across and then down, in place.
void blur(std::vector<float>& values, int width, int height, int radius) {
    if (radius <= 0)
        return;
    std::vector<float> line(static_cast<std::size_t>(std::max(width, height)));
    const float span = static_cast<float>(2 * radius + 1);
    for (int y = 0; y < height; ++y) {
        float* row = values.data() + static_cast<std::size_t>(y) * width;
        for (int x = 0; x < width; ++x) {
            float sum = 0;
            for (int k = -radius; k <= radius; ++k)
                sum += row[std::clamp(x + k, 0, width - 1)];
            line[static_cast<std::size_t>(x)] = sum / span;
        }
        std::copy(line.begin(), line.begin() + width, row);
    }
    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            float sum = 0;
            for (int k = -radius; k <= radius; ++k)
                sum += values[static_cast<std::size_t>(std::clamp(y + k, 0, height - 1)) * width + x];
            line[static_cast<std::size_t>(y)] = sum / span;
        }
        for (int y = 0; y < height; ++y)
            values[static_cast<std::size_t>(y) * width + x] = line[static_cast<std::size_t>(y)];
    }
}

// One face: the bone stock, the finish's dimples, the raised ink and the glint facets, `grain`
// texels between facets. `ink` and `foil` are per texel; empty for a plain face.
CardFaceMap print_face(int width, int height, double density, double grain, std::uint32_t seed,
                       const std::vector<float>& finish, const std::vector<float>& ink, const std::vector<float>& foil) {
    CardFaceMap map;
    map.width = width;
    map.height = height;
    const std::size_t count = static_cast<std::size_t>(width) * height;
    map.texels.resize(count);
    paper::Parameters stock = paper::preset(kStock.preset);
    stock.colour = {kStock.colour[0], kStock.colour[1], kStock.colour[2]};
    stock.machine_grain = 1.0;
    stock.pixels_per_point = density;
    stock.seed = seed;
    paper::Image sheet;
    const bool made = paper::generate_sheet(stock, width, height, paper::Shading{}, sheet);
    for (std::size_t i = 0; i < count; ++i) {
        CardTexel& t = map.texels[i];
        const double r = made ? sheet.rgba[i * 4] / 255.0 : kStock.colour[0];
        const double g = made ? sheet.rgba[i * 4 + 1] / 255.0 : kStock.colour[1];
        const double b = made ? sheet.rgba[i * 4 + 2] / 255.0 : kStock.colour[2];
        t.red = static_cast<float>(linear_of(r));
        t.green = static_cast<float>(linear_of(g));
        t.blue = static_cast<float>(linear_of(b));
    }
    // The raised ink's height, points, softened at its edges.
    std::vector<float> rise;
    if (!ink.empty()) {
        rise = ink;
        const int radius = std::max(1, static_cast<int>(std::lround(.18 * density)));
        blur(rise, width, height, radius);
        blur(rise, width, height, radius);
    }
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * width + x;
            CardTexel& t = map.texels[i];
            double sx = finish[i * 2], sy = finish[i * 2 + 1];
            if (!rise.empty()) {
                const std::size_t left = static_cast<std::size_t>(y) * width + std::max(x - 1, 0);
                const std::size_t right = static_cast<std::size_t>(y) * width + std::min(x + 1, width - 1);
                const std::size_t up = static_cast<std::size_t>(std::max(y - 1, 0)) * width + x;
                const std::size_t down = static_cast<std::size_t>(std::min(y + 1, height - 1)) * width + x;
                // rise over run: points of height over points across
                sx += kInkHeight * (rise[right] - rise[left]) * .5 * density;
                sy += kInkHeight * (rise[down] - rise[up]) * .5 * density;
                t.ink = ink[i];
                t.foil = foil[i];
            }
            t.slope_x = static_cast<float>(sx);
            t.slope_y = static_cast<float>(sy);
        }
    scatter_card_facets(map, grain, seed * 2654435761U + 17U);
    return map;
}

}  // namespace

BusinessCard::BusinessCard(gf::StableId id, Kind kind)
    : Control(std::move(id)), kind_(kind),
      remarks_(static_cast<std::uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count())) {
    set_paint_plane(gf::PaintPlane::overlay);
    set_focusable(true);
    if (kind_ == Kind::page) {
        set_accessible_name("Rainstar Software business card");
        set_accessible_description("Opens the card (Enter).");
    } else {
        set_accessible_name("Rainstar Software business card, in the hand");
        set_accessible_description(std::string(contact) + ". Enter opens " + std::string(website) +
                                   "; the arrow keys turn the card; Escape puts it away.");
    }
    for (int c = 0; c < 3; ++c) {
        material_.edge[c] = linear_of(kStock.colour[c]) * .78;
        material_.ink[c] = kStock.ink[c];
    }
    turn_ = rest(true);
    lift_ = in_hand() ? 0 : kLiftResting;
    start_ = std::chrono::steady_clock::now();
    place_seed_ ^= static_cast<std::uint32_t>(start_.time_since_epoch().count() >> 7);
    if (place_seed_ == 0)
        place_seed_ = 0x51ed2701U;
}

BusinessCard::~BusinessCard() = default;

void BusinessCard::initialize_control_tree() {
    if (!in_hand())
        return;
    std::shared_ptr<HelpGlyph> mark = gf::make_control<HelpGlyph>(gf::StableId(std::string(stable_id().value()) + ".close"), "×");
    (*mark).set_ink(gf::Color::rgba(228, 220, 196));
    (*mark).set_accessible_name("Put the card away (Escape)");
    (*mark).set_paint_plane(gf::PaintPlane::overlay);
    gf::on((*mark).clicked(), *this, &BusinessCard::put_away);
    close_mark_ = mark;
    add_child(mark);
}

bool BusinessCard::in_hand() const {
    const bool hand = kind_ == Kind::hand;
    return hand;
}

double BusinessCard::clock() const {
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start_;
    return elapsed.count();
}

// Texels a point for the faces: on the page, finer than the screen, for its small lettering; in
// the hand, a little finer than the screen at the size it is shown.
double BusinessCard::wanted_density() const {
    const double scale = attached_window() != nullptr ? std::max(.5, (*attached_window()).scale()) : 1.0;
    const double density = in_hand() ? std::min(std::max(1.0, scale * 1.15), 2600 / std::max(1.0, width_))
                                     : std::min(std::max(2.0, scale * 1.25), 1100 / std::max(1.0, width_));
    return density;
}

// At rest the card lies a little turned, as if just set down: its left edge away, its foot
// tipped toward the eye, a degree or two askew. Face down, the same, turned over side to side.
Turn BusinessCard::rest(bool front) const {
    const Turn askew = turn_then(turn_then(turn_about(0, 1, 0, -.12), turn_about(1, 0, 0, .10)), turn_about(0, 0, 1, -.035));
    if (front)
        return askew;
    const Turn over = turn_then(turn_about(0, 1, 0, kPi), askew);
    return over;
}

CardPose BusinessCard::pose() const {
    CardPose p;
    p.width = width_;
    p.height = width_ * kAspect;
    p.thickness = std::max(.8, width_ * kThickness);
    p.centre_x = picture_points_ / 2;
    p.centre_y = picture_points_ / 2;
    p.lift = lift_;
    p.eye = width_ * 6;
    p.turn = turn_;
    p.foil = foil_;
    p.shadow = in_hand() ? 0 : 1;
    return p;
}

gf::Point BusinessCard::picture_origin() const {
    const gf::Point origin{centre_.x - picture_points_ / 2, centre_.y - picture_points_ / 2};
    return origin;
}

void BusinessCard::resize(double width) {
    if (std::abs(width - width_) <= .5)
        return;
    width_ = width;
    // room for the card turned any way, and on the page for the longest shadow it casts
    const double diagonal = std::hypot(width_, width_ * kAspect) * 1.15;
    picture_points_ = std::ceil(in_hand() ? diagonal + 40 : diagonal + width_ * .5 + 90);
    forget();
    wake();
}

void BusinessCard::place(gf::Point centre, double width, double top_room) {
    const bool moved = centre.x != centre_.x || centre.y != centre_.y || top_room != top_room_;
    centre_ = centre;
    top_room_ = top_room;
    resize(width);
    if (moved)
        invalidate(gf::Dirty::paint);
}

void BusinessCard::arrange(gf::Rect bounds) {
    arrange_self(bounds);
    if (!in_hand())
        return;
    // In the hand: as large as leaves room round it for the remarks, a little above the middle.
    const double width = std::clamp(std::min(bounds.width * .58, bounds.height * .52 / kAspect), 180.0, 1000.0);
    centre_ = {bounds.width / 2, bounds.height / 2 - 8};
    resize(width);
    if (close_mark_)
        set_child_layout(close_mark_, {bounds.width - 52, 12, 40, 40});
}

void BusinessCard::set_showing(bool showing) {
    if (showing == showing_)
        return;
    showing_ = showing;
    hovering_ = false;
    over_company_ = false;
    pressed_on_card_ = pressed_on_black_ = false;
    spin_x_ = spin_y_ = 0;
    turn_ = rest(true);
    lift_ = in_hand() ? 0 : kLiftResting;
    foil_ = 0;
    said_.clear();
    if (showing_) {
        stale_ = true;
        wake();
        return;
    }
    // Hidden: free the faces until it is shown again.
    forget();
    if (timer_)
        (*timer_).stop();
}

void BusinessCard::on_attached_to_window() {
    timer_ = std::make_unique<gf::Timer>(*attached_window(), std::chrono::milliseconds(16));
    gf::on((*timer_).tick(), *this, &BusinessCard::tick);
    wake();
}

void BusinessCard::on_detaching_from_window(gf::Window& window) noexcept {
    if (timer_)
        (*timer_).stop();
    timer_.reset();
    if (image_.value)
        static_cast<void>(window.remove_image(image_));
    image_ = {};
    image_scale_ = 0;
    front_ = {};
    back_ = {};
    letters_.clear();
    address_ = {};
    text_.reset();
}

// Lets go of the faces, the lettering and the picture; made again when next wanted.
void BusinessCard::forget() {
    front_ = {};
    back_ = {};
    letters_.clear();
    address_ = {};
    built_width_ = 0;
    density_ = 0;
    pixels_.clear();
    pixels_.shrink_to_fit();
    drawn_ = {};
    stale_ = true;
    if (image_.value && attached_window() != nullptr)
        static_cast<void>((*attached_window()).remove_image(image_));
    image_ = {};
    image_scale_ = 0;
}

void BusinessCard::wake() {
    if (timer_ && showing_ && !(*timer_).enabled())
        (*timer_).start();
}

// The card's lettering shaped at the faces' density, a letter at a time for the spaced capitals.
void BusinessCard::shape_lettering() {
    if (text_failed_ || attached_window() == nullptr)
        return;
    density_ = wanted_density();
    const double height = width_ * kAspect;
    try {
        if (!text_)
            text_ = std::make_unique<GameText>(std::filesystem::path(asset_directory()) / "fonts", "LibreBaskerville");
        (*text_).begin();
        letters_.clear();
        for (char c : kCompany) {
            if (c == ' ')
                continue;
            const std::string glyph(1, c);
            bool known = false;
            for (const Letter& letter : letters_)
                known = known || letter.text == glyph;
            if (known)
                continue;
            Letter letter{glyph, (*text_).get(glyph, false, kCompanySize * height, 0, density_)};
            letters_.push_back(std::move(letter));
        }
        address_ = Letter{std::string(contact), (*text_).get(contact, false, kAddressSize * height, 0, density_)};
    } catch (...) {
        text_failed_ = true;
        letters_.clear();
        address_ = {};
    }
}

void BusinessCard::build_faces() {
    const double height_points = width_ * kAspect;
    const int width = static_cast<int>(std::lround(width_ * density_));
    const int height = static_cast<int>(std::lround(height_points * density_));
    if (width < 16 || height < 16)
        return;
    const std::size_t count = static_cast<std::size_t>(width) * height;
    std::vector<float> ink(count, 0.0F), foil(count, 0.0F);
    // The company in spaced capitals, centred.
    const double size = kCompanySize * height_points;
    double run = 0;
    int glyphs = 0;
    for (char c : kCompany) {
        if (c == ' ') {
            run += kCompanySpace * size;
            continue;
        }
        for (const Letter& letter : letters_)
            if (letter.text[0] == c && letter.image.mask.has_value())
                run += letter.image.mask.metrics().logical_width;
        ++glyphs;
    }
    run += kCompanyTracking * size * (glyphs - 1);
    double line_height = size * 1.25;
    if (!letters_.empty() && letters_.front().image.mask.has_value())
        line_height = letters_.front().image.mask.metrics().logical_height;
    const double left = (width_ - run) / 2, top = kCompanyMiddle * height_points - line_height / 2;
    double x = left;
    for (char c : kCompany) {
        if (c == ' ') {
            x += kCompanySpace * size;
            continue;
        }
        for (const Letter& letter : letters_)
            if (letter.text[0] == c && letter.image.mask.has_value()) {
                lay_ink(letter.image, x, top, density_, width, height, ink, foil, true);
                x += letter.image.mask.metrics().logical_width + kCompanyTracking * size;
            }
    }
    const double pad = .025;
    company_ = {left / width_ - pad, top / height_points - pad, run / width_ + 2 * pad,
                line_height / height_points + 2 * pad};
    // The address, centred beneath.
    if (address_.image.mask.has_value()) {
        const gui_forms::TextMaskMetrics m = address_.image.mask.metrics();
        lay_ink(address_.image, (width_ - m.logical_width) / 2, kAddressMiddle * height_points - m.logical_height / 2,
                density_, width, height, ink, foil, false);
    }
    // The finish the Settings screen chooses for the playing cards, at their fineness: dimples
    // and glint facets as many device pixels apart as on the table (card_press.cpp), here in
    // texels; their depth, sparkle and glare as there.
    const SuiteSettings& chosen = SettingsStore::shared().values();
    const double scale = attached_window() != nullptr ? std::max(.5, (*attached_window()).scale()) : 1.0;
    const double texels_a_pixel = density_ / scale;
    const double pitch = (.8 + 5.2 * chosen.card_emboss_size) * texels_a_pixel;
    const double grain = (1.5 + 6.5 * chosen.card_grain) * texels_a_pixel;
    std::vector<float> finish;
    card_finish_slopes(width, height, pitch, finish);
    double spread = 0;
    for (float s : finish)
        spread += static_cast<double>(s) * s;
    spread = std::sqrt(spread / std::max<std::size_t>(1, finish.size()));
    const double depth = kFinishSlope * chosen.card_emboss / .4;
    const float gain = static_cast<float>(spread > 1e-9 ? depth / spread : 0);
    for (float& s : finish)
        s *= gain;
    material_.sparkle = chosen.card_sparkle;
    material_.sheen = chosen.card_glare / .7;
    front_ = card_face_levels(print_face(width, height, density_, std::max(1.0, grain), 1931U, finish, ink, foil));
    back_ = card_face_levels(print_face(width, height, density_, std::max(1.0, grain), 2207U, finish, {}, {}));
    built_width_ = width_;
    stale_ = true;
}

// Draws the card where it now is into the picture, and hands the window the pixels that changed.
void BusinessCard::draw() {
    gf::Window* window = attached_window();
    if (window == nullptr || front_.levels.empty())
        return;
    const double scale = std::max(.5, (*window).scale());
    const int side = static_cast<int>(std::ceil(picture_points_ * scale));
    if (!image_.value || image_scale_ != scale || side != picture_pixels_) {
        if (image_.value)
            static_cast<void>((*window).remove_image(image_));
        image_ = {};
        picture_pixels_ = side;
        pixels_.assign(static_cast<std::size_t>(side) * side * 4, std::byte{0});
        const gf::ImageLoadResult loaded = (*window).load_bgra32_premultiplied(
            static_cast<std::uint32_t>(side), static_cast<std::uint32_t>(side), static_cast<std::uint64_t>(side) * 4, pixels_);
        if (!loaded)
            return;
        image_ = loaded.image;
        image_scale_ = scale;
        drawn_ = {};
        stale_ = true;
    }
    const CardPose now = pose();
    const bool same = !stale_ && turn_apart(now.turn, shown_.turn) < 1e-5 && std::abs(now.lift - shown_.lift) < .01 &&
                      std::abs(now.foil - shown_.foil) < .002;
    if (same)
        return;
    CardPicture picture{pixels_.data(), side, side, static_cast<std::size_t>(side) * 4, scale};
    const std::size_t stride = picture.stride;
    for (int y = drawn_.y0; y < drawn_.y1; ++y)
        std::fill(pixels_.begin() + static_cast<std::ptrdiff_t>(y * stride + static_cast<std::size_t>(drawn_.x0) * 4),
                  pixels_.begin() + static_cast<std::ptrdiff_t>(y * stride + static_cast<std::size_t>(drawn_.x1) * 4),
                  std::byte{0});
    const PixelBox box = held_card_box(now, picture);
    draw_held_card(front_, back_, material_, now, picture, box);
    const PixelBox changed = box_union(drawn_, box);
    drawn_ = box;
    shown_ = now;
    stale_ = false;
    if (box_empty(changed))
        return;
    const int w = changed.x1 - changed.x0, h = changed.y1 - changed.y0;
    patch_.resize(static_cast<std::size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y)
        std::copy(pixels_.begin() + static_cast<std::ptrdiff_t>((changed.y0 + y) * stride + static_cast<std::size_t>(changed.x0) * 4),
                  pixels_.begin() + static_cast<std::ptrdiff_t>((changed.y0 + y) * stride + static_cast<std::size_t>(changed.x1) * 4),
                  patch_.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * w * 4));
    const gf::Point origin = picture_origin();
    const gf::Rect damage{origin.x + changed.x0 / scale, origin.y + changed.y0 / scale, w / scale, h / scale};
    // A patch gives the image a new generation, and so a new id to draw it by.
    const gf::ImageLoadResult patched = (*window).patch_bgra32_premultiplied(
        image_, static_cast<std::uint32_t>(changed.x0), static_cast<std::uint32_t>(changed.y0), static_cast<std::uint32_t>(w),
        static_cast<std::uint32_t>(h), static_cast<std::uint64_t>(w) * 4, patch_, *this, damage);
    if (patched)
        image_ = patched.image;
    else
        stale_ = true;
    invalidate(inflated(damage, 1));
}

void BusinessCard::tick() {
    const double now = clock();
    const double dt = std::clamp(now - now_, 0.0, .1);
    now_ = now;
    if (!showing_ || attached_window() == nullptr || width_ <= 0) {
        if (timer_)
            (*timer_).stop();
        return;
    }
    // The faces, once their lettering is shaped.
    if (built_width_ != width_ || (density_ > 0 && std::abs(wanted_density() - density_) > 1e-9)) {
        if (!front_.levels.empty())
            forget();
        shape_lettering();
        if (text_ && (*text_).ready())
            build_faces();
        else if (!text_failed_)
            return;  // the timer comes back for the lettering
    }
    // The turn: in the hand it follows the pointer; let go, it spins down and settles.
    bool moving = false;
    const double spin = std::hypot(spin_x_, spin_y_);
    const bool holding = pressed_on_card_ && in_hand();
    if (holding) {
        if (now - last_time_ > .08) {
            spin_x_ *= std::exp(-12 * dt);
            spin_y_ *= std::exp(-12 * dt);
        }
        moving = true;
    } else {
        Turn target = rest(front_toward_eye(turn_));
        if (hovering_) {
            const double h = width_ * kAspect;
            const double ox = std::clamp((pointer_.x - centre_.x) / (width_ / 2), -1.0, 1.0);
            const double oy = std::clamp((pointer_.y - centre_.y) / (h / 2), -1.0, 1.0);
            const double reach = std::min(1.0, std::hypot(ox, oy));
            if (reach > 1e-3)
                target = turn_then(target, turn_about(-oy, ox, 0, (in_hand() ? .10 : .07) * reach));
        }
        if (spin > 1.0) {
            turn_ = turn_then(turn_, turn_about(spin_x_, spin_y_, 0, spin * dt));
            spin_x_ *= std::exp(-2.4 * dt);
            spin_y_ *= std::exp(-2.4 * dt);
            moving = true;
        } else {
            if (spin > 1e-3)
                turn_ = turn_then(turn_, turn_about(spin_x_, spin_y_, 0, spin * dt));
            spin_x_ *= std::exp(-6 * dt);
            spin_y_ *= std::exp(-6 * dt);
            turn_ = turn_toward(turn_, target, 1 - std::exp(-5 * dt));
            moving = moving || turn_apart(turn_, target) > .0015 || spin > .01;
        }
    }
    if (!in_hand()) {
        const double lift_target = hovering_ ? kLiftHovered : kLiftResting;
        lift_ = approach(lift_, lift_target, 8, dt);
        moving = moving || std::abs(lift_ - lift_target) > .02;
    }
    const double foil_target = in_hand() && over_company_ && front_toward_eye(turn_) ? 1.0 : 0.0;
    foil_ = approach(foil_, foil_target, 10, dt);
    if (std::abs(foil_ - foil_target) < .004)
        foil_ = foil_target;
    moving = moving || foil_ != foil_target;
    draw();
    // The remarks: said one at a time while the pointer is over the card in the hand, each fading.
    const std::size_t before = said_.size();
    for (std::size_t i = 0; i < said_.size();) {
        invalidate(inflated(said_[i].box, 2));
        if (now - said_[i].born > said_[i].life)
            said_.erase(said_.begin() + static_cast<std::ptrdiff_t>(i));
        else
            ++i;
    }
    if (in_hand() && hovering_ && now >= next_remark_ && static_cast<int>(said_.size()) < kRemarksAtOnce) {
        say();
        next_remark_ = now + kRemarkGapMin + kRemarkGapSpread * unit_random(place_seed_);
    }
    const bool talking = !said_.empty() || before > 0 || (in_hand() && hovering_);
    if (!timer_)
        return;
    if (moving)
        (*timer_).set_interval(std::chrono::milliseconds(16));
    else if (talking)
        (*timer_).set_interval(std::chrono::milliseconds(33));
    else
        (*timer_).stop();
}

// A remark placed at random around the card: clear of it and of the other remarks, near enough
// to be about it.
void BusinessCard::say() {
    const std::string text = remarks_.next();
    const double width = 16 * .5 * static_cast<double>(text.size()) + 8, height = 26;
    const gf::Rect bounds = client_rectangle();
    const double card_height = width_ * kAspect;
    const gf::Rect card{centre_.x - width_ * .55, centre_.y - card_height * .6, width_ * 1.1, card_height * 1.2};
    for (int attempt = 0; attempt < 60; ++attempt) {
        const double angle = 2 * kPi * unit_random(place_seed_);
        const double reach = .58 * width_ + .12 * width_ * unit_random(place_seed_);
        const double x = centre_.x + std::cos(angle) * reach - width / 2;
        const double y = centre_.y + std::sin(angle) * reach * .62 - height / 2;
        const gf::Rect box{x, y, width, height};
        if (x < 12 || y < 56 || x + width > bounds.width - 12 || y + height > bounds.height - 40)
            continue;
        if (overlaps(box, card))
            continue;
        bool crowded = false;
        for (const Remark& other : said_)
            crowded = crowded || overlaps(inflated(box, 8), other.box);
        if (crowded)
            continue;
        said_.push_back({text, box, now_, 2.4 + .045 * static_cast<double>(text.size())});
        invalidate(inflated(box, 2));
        return;
    }
}

void BusinessCard::visit() {
    open_link("https://" + std::string(website) + "/");
}

void BusinessCard::put_away() {
    if (close)
        close();
}

bool BusinessCard::card_hit(gf::Point local, bool& company) const {
    company = false;
    if (front_.levels.empty() || !showing_ || local.y < -top_room_)
        return false;
    const gf::Point origin = picture_origin();
    const CardHit hit = held_card_hit(pose(), local.x - origin.x, local.y - origin.y);
    company = hit.hit && hit.front && hit.u >= company_.x && hit.u <= company_.x + company_.width &&
              hit.v >= company_.y && hit.v <= company_.y + company_.height;
    return hit.hit;
}

bool BusinessCard::hit_test_local(gf::Point local) const {
    // In the hand it holds the whole window; on the page, only the card.
    if (in_hand())
        return showing_;
    bool company = false;
    const bool hit = card_hit(local, company);
    return hit;
}

gf::Insets BusinessCard::visual_outsets() const noexcept {
    const gf::Insets outsets{8, in_hand() ? 0.0 : std::max(0.0, top_room_), 8, 8};
    return outsets;
}

void BusinessCard::hover(gf::Point local) {
    bool company = false;
    const bool hit = card_hit(local, company);
    pointer_ = local;
    if (hit && !hovering_)
        next_remark_ = now_ + kFirstRemark;
    hovering_ = hit;
    // On the page the whole card is the way in; in the hand, the company's name the way out.
    const bool pointing = in_hand() ? company : hit;
    if (pointing != over_company_) {
        over_company_ = pointing;
        if (pointing)
            set_cursor(gf::CursorKind::hand);
        else
            set_cursor(std::nullopt);
    }
    wake();
}

void BusinessCard::unhover() {
    hovering_ = false;
    if (over_company_) {
        over_company_ = false;
        set_cursor(std::nullopt);
    }
    wake();
}

void BusinessCard::on_pointer(gf::PointerEvent& e) {
    const gf::Point local = point_from_window(e.position);
    now_ = clock();
    if (e.action == gf::PointerAction::wheel) {
        if (!in_hand() && scroll)
            scroll(e);
        e.handled = true;
        return;
    }
    if (e.action == gf::PointerAction::leave) {
        if (!pressed_on_card_)
            unhover();
        return;
    }
    if (e.action == gf::PointerAction::down && e.button == gf::PointerButton::primary) {
        bool company = false;
        const bool hit = card_hit(local, company);
        if (!hit && !in_hand())
            return;
        if (attached_window() != nullptr)
            static_cast<void>((*attached_window()).request_focus(shared_from_this()));
        set_pointer_capture(true);
        pressed_on_card_ = hit;
        pressed_on_black_ = !hit;
        dragged_ = false;
        pressed_ = local;
        last_ = local;
        last_time_ = now_;
        if (hit && in_hand())
            spin_x_ = spin_y_ = 0;
        e.handled = true;
        wake();
        return;
    }
    if (e.action == gf::PointerAction::up && (pressed_on_card_ || pressed_on_black_)) {
        set_pointer_capture(false);
        const bool on_card = pressed_on_card_, on_black = pressed_on_black_;
        pressed_on_card_ = pressed_on_black_ = false;
        const double fling = std::hypot(spin_x_, spin_y_);
        if (fling > 25) {
            spin_x_ *= 25 / fling;
            spin_y_ *= 25 / fling;
        }
        bool company = false;
        const bool hit = card_hit(local, company);
        e.handled = true;
        if (!dragged_ && on_card && hit && !in_hand()) {
            if (open)
                open();
            return;
        }
        if (!dragged_ && on_card && hit && company)
            visit();
        if (!dragged_ && on_black && !hit) {
            put_away();
            return;
        }
        hover(local);
        return;
    }
    if (e.action == gf::PointerAction::move) {
        if (std::hypot(local.x - pressed_.x, local.y - pressed_.y) > 3)
            dragged_ = true;
        if (!pressed_on_card_ || !in_hand()) {
            hover(local);
            return;
        }
        const double dx = local.x - last_.x, dy = local.y - last_.y;
        const double distance = std::hypot(dx, dy);
        if (distance > 0) {
            // A card's width dragged turns it half over.
            const double per_point = kPi / std::max(1.0, width_);
            turn_ = turn_then(turn_, turn_about(-dy, dx, 0, distance * per_point));
            const double dt = std::max(1e-3, now_ - last_time_);
            spin_x_ = .5 * spin_x_ + .5 * (-dy * per_point / dt);
            spin_y_ = .5 * spin_y_ + .5 * (dx * per_point / dt);
            last_time_ = now_;
        }
        last_ = local;
        pointer_ = local;
        e.handled = true;
        wake();
    }
}

void BusinessCard::on_key(gf::KeyEvent& e) {
    if (e.handled || e.action != gf::KeyAction::down)
        return;
    const bool activate = e.physical_key == gf::PhysicalKey::enter || e.physical_key == gf::PhysicalKey::space;
    if (!in_hand()) {
        if (!activate)
            return;
        if (open)
            open();
        e.handled = true;
        return;
    }
    const double nudge = 6;  // radians a second
    if (activate) {
        visit();
    } else if (e.physical_key == gf::PhysicalKey::escape) {
        put_away();
    } else if (e.physical_key == gf::PhysicalKey::left) {
        spin_y_ -= nudge;
    } else if (e.physical_key == gf::PhysicalKey::right) {
        spin_y_ += nudge;
    } else if (e.physical_key == gf::PhysicalKey::up) {
        spin_x_ += nudge;
    } else if (e.physical_key == gf::PhysicalKey::down) {
        spin_x_ -= nudge;
    } else {
        return;
    }
    e.handled = true;
    wake();
}

void BusinessCard::on_focus_cue_changed(bool visible) {
    focused_ = visible;
    invalidate(gf::Dirty::paint);
}

void BusinessCard::on_paint(gf::Painter& p, gf::Rect) {
    const gf::Rect bounds = client_rectangle();
    if (in_hand())
        p.fill_rect(bounds, gf::Color::rgba(0, 0, 0));
    p.save();
    if (!in_hand())
        p.clip_rect({-8, -top_room_, bounds.width + 16, bounds.height + top_room_ + 8});
    if (image_.value) {
        const gf::Point origin = picture_origin();
        p.draw_image(image_, {origin.x, origin.y, picture_points_, picture_points_});
    }
    if (focused_ && width_ > 0 && !in_hand()) {
        const double h = width_ * kAspect;
        p.stroke_rect({centre_.x - width_ / 2 - 6, centre_.y - h / 2 - 6, width_ + 12, h + 12},
                      gf::Color::rgba(110, 91, 37, 160), 1);
    }
    p.restore();
    if (!in_hand())
        return;
    // The remarks, in the stock's own colour on the black.
    for (const Remark& remark : said_) {
        const double age = now_ - remark.born;
        const double rise = std::min(1.0, age / .35), fall = std::min(1.0, (remark.life - age) / .9);
        const double a = std::clamp(std::min(rise, fall), 0.0, 1.0);
        if (a <= 0)
            continue;
        const double width = p.measure_text_utf8(remark.text, kRemarkFont).width;
        const double x = remark.box.x + (remark.box.width - width) / 2;
        const double drift = 3 * (1 - rise);
        p.draw_text_utf8({x, remark.box.y + drift + 19}, remark.text, kRemarkFont,
                         gf::Color::rgba(226, 219, 198, static_cast<std::uint8_t>(std::lround(235 * a))));
    }
    const double hint = p.measure_text_utf8(kHint, kHintFont).width;
    p.draw_text_utf8({(bounds.width - hint) / 2, bounds.height - 18}, kHint, kHintFont, gf::Color::rgba(128, 120, 102));
}

}  // namespace games
