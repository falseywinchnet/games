#include "reader.hpp"

#include "paper.hpp"

#include <algorithm>
#include <cmath>

namespace pl {
namespace {

using page::LayoutUnit;

// The room around the open book: the shelf's wall, in the shade of reading light.
constexpr float wall_shade = 0.5f;
// The margin left round the spread, as a fraction of the shorter side.
constexpr double margin_fraction = 0.035;

page::PlacedRun run_of(page::Typesetter& setter, const page::TextStyle& style, const std::string& text,
                       LayoutUnit x, LayoutUnit baseline, bool centred) {
    const page::ShapedRun& shaped = setter.shaped(style, text, false);
    page::PlacedRun run;
    run.face = style.face;
    run.em = style.em;
    run.ink = style.ink;
    run.x = centred ? x - shaped.advance / 2 : x;
    run.baseline = baseline;
    run.glyphs = shaped.glyphs;
    return run;
}

}  // namespace

void Reader::open(const Work& work, const LayoutOptions& options, const Place& place) {
    work_ = &work;
    options_ = options;
    plan_sections(work, sections_);
    section_ = sections_.empty() ? 0 : std::min(place.section, sections_.size() - 1);
    page_ = place.page;
    contents_ = false;
    pages_.clear();
    laid_section_ = static_cast<std::size_t>(-1);
    contents_pages_.clear();
    entries_.clear();
    contents_page_ = 0;
}

void Reader::close() {
    work_ = nullptr;
    sections_.clear();
    pages_.clear();
    contents_pages_.clear();
    entries_.clear();
    laid_section_ = static_cast<std::size_t>(-1);
    contents_ = false;
}

Place Reader::place() const {
    Place place;
    if (work_ != nullptr)
        place.book = (*work_).id;
    place.section = section_;
    place.page = page_;
    return place;
}

std::string Reader::describe() const {
    if (work_ == nullptr || sections_.empty())
        return std::string();
    if (contents_)
        return "Contents";
    const Section& section = sections_[section_];
    std::string title = section.title;
    if (section.division < (*work_).divisions.size() && title == (*work_).divisions[section.division].short_title)
        title = (*work_).divisions[section.division].short_title;
    const std::size_t total = pages_.size();
    if (total == 0)
        return title;
    const std::size_t first = page_ + 1;
    const bool both = !geometry_.folded && page_ + 1 < total;
    return title + (both ? ", pages " + std::to_string(first) + " and " + std::to_string(first + 1)
                         : ", page " + std::to_string(first)) +
           " of " + std::to_string(total);
}

void Reader::set_step(page::TypeStep step) {
    options_.step = step;
}

void Reader::ensure_laid_out(Printer& printer) {
    if (work_ == nullptr || sections_.empty() || !printer.ready())
        return;
    if (laid_section_ == section_ && laid_step_ == options_.step)
        return;
    lay_out_section(*work_, sections_[section_], options_, printer.typesetter(), pages_);
    laid_section_ = section_;
    laid_step_ = options_.step;
    if (pages_.empty())
        page_ = 0;
    else if (page_ >= pages_.size())
        page_ = pages_.size() - 1;
    if (!geometry_.folded)
        page_ -= page_ % 2;
}

bool Reader::next(Printer& printer) {
    if (work_ == nullptr)
        return false;
    const std::size_t step = geometry_.folded ? 1 : 2;
    if (contents_) {
        lay_out_contents(printer);
        if (contents_page_ + step < contents_pages_.size()) {
            contents_page_ += step;
            return true;
        }
        return false;
    }
    ensure_laid_out(printer);
    if (page_ + step < pages_.size()) {
        page_ += step;
        return true;
    }
    if (section_ + 1 < sections_.size()) {
        ++section_;
        page_ = 0;
        return true;
    }
    return false;
}

bool Reader::previous(Printer& printer) {
    if (work_ == nullptr)
        return false;
    const std::size_t step = geometry_.folded ? 1 : 2;
    if (contents_) {
        if (contents_page_ >= step) {
            contents_page_ -= step;
            return true;
        }
        return false;
    }
    if (page_ >= step) {
        page_ -= step;
        return true;
    }
    if (section_ == 0)
        return false;
    --section_;
    page_ = static_cast<std::size_t>(-1) / 2;  // clamped to the last page when laid out
    ensure_laid_out(printer);
    return true;
}

void Reader::to_section(std::size_t section) {
    if (section >= sections_.size())
        return;
    section_ = section;
    page_ = 0;
    contents_ = false;
}

void Reader::show_contents(bool shown) {
    contents_ = shown && work_ != nullptr;
    if (contents_) {
        // Open the contents where the section being read is listed.
        for (const Entry& entry : entries_) {
            if (entry.section == section_) {
                contents_page_ = entry.page - (geometry_.folded ? 0 : entry.page % 2);
                break;
            }
        }
    }
}

void Reader::lay_out_contents(Printer& printer) {
    if (!contents_pages_.empty() || work_ == nullptr || !printer.ready())
        return;
    page::Typesetter& setter = printer.typesetter();
    const page::FaceId roman = roman_face(*work_);
    page::TextStyle entry;
    entry.face = roman;
    entry.em = body_em(*work_, options_.step);
    page::TextStyle title = entry;
    title.em = entry.em * 3 / 2;
    page::TextStyle head = entry;
    head.em = entry.em * 78 / 100;
    head.ink = page::Ink::soft;
    head.tracking = head.em * 14 / 100;
    const LayoutUnit line = entry.em * 150 / 100;
    const LayoutUnit bottom = page::content_top_lu + page::content_height_lu - line;
    page::PageLayout current;
    LayoutUnit y = page::content_top_lu + title.em * 2;
    current.runs.push_back(run_of(setter, title, "Contents", page::content_left_lu + page::content_width_lu / 2, y,
                                  true));
    y += title.em;
    std::size_t last_division = static_cast<std::size_t>(-1);
    for (std::size_t s = 0; s < sections_.size(); ++s) {
        const Section& section = sections_[s];
        const bool new_division = section.division != last_division && (*work_).divisions.size() > 1;
        const LayoutUnit needed = line + (new_division ? line / 2 : 0);
        if (y + needed > bottom) {
            contents_pages_.push_back(std::move(current));
            current = page::PageLayout{};
            y = page::content_top_lu + line;
        }
        if (new_division && s > 0)
            y += line / 2;
        last_division = section.division;
        y += line;
        current.runs.push_back(run_of(setter, entry, section.title, page::content_left_lu + entry.em * 2, y, false));
        Entry made;
        made.page = contents_pages_.size();
        made.y0 = y - line * 3 / 4;
        made.y1 = y + line / 4;
        made.section = s;
        entries_.push_back(made);
    }
    contents_pages_.push_back(std::move(current));
    if (contents_pages_.size() % 2 == 1)
        contents_pages_.push_back(page::PageLayout{});
    for (std::size_t p = 0; p < contents_pages_.size(); ++p) {
        contents_pages_[p].number = static_cast<std::uint32_t>(p + 1);
        const std::string number = std::to_string(p + 1);
        if (!contents_pages_[p].runs.empty())
            contents_pages_[p].runs.push_back(run_of(setter, head, number,
                                                     page::content_left_lu + page::content_width_lu / 2,
                                                     page::content_top_lu + page::content_height_lu, true));
    }
    show_contents(contents_);
}

int Reader::contents_entry_at(int x, int y) const {
    if (!contents_)
        return -1;
    for (int side = 0; side < 2; ++side) {
        const page::PageRect& rect = side == 0 ? geometry_.left : geometry_.right;
        if (rect.height == 0 || x < rect.x || y < rect.y || x >= rect.x + static_cast<std::int32_t>(rect.width) ||
            y >= rect.y + static_cast<std::int32_t>(rect.height))
            continue;
        const std::size_t shown = contents_page_ + static_cast<std::size_t>(side);
        const double units = 1.0e6 / static_cast<double>(rect.height);
        const LayoutUnit at = static_cast<LayoutUnit>((y - rect.y) * units);
        const LayoutUnit across = static_cast<LayoutUnit>((x - rect.x) * units);
        if (across < page::content_left_lu || across > page::content_left_lu + page::content_width_lu)
            return -1;
        for (const Entry& entry : entries_)
            if (entry.page == shown && at >= entry.y0 && at < entry.y1)
                return static_cast<int>(entry.section);
    }
    return -1;
}

int Reader::side_at(int x, int y) const {
    const page::PageRect& left = geometry_.left;
    const page::PageRect& right = geometry_.right;
    const bool in_left = left.height > 0 && x >= left.x && x < left.x + static_cast<std::int32_t>(left.width) &&
                         y >= left.y && y < left.y + static_cast<std::int32_t>(left.height);
    if (in_left)
        return geometry_.folded ? 1 : -1;
    const bool in_right = right.height > 0 && x >= right.x &&
                          x < right.x + static_cast<std::int32_t>(right.width) && y >= right.y &&
                          y < right.y + static_cast<std::int32_t>(right.height);
    return in_right ? 1 : 0;
}

const std::vector<std::uint8_t>& Reader::sheet(std::uint32_t width, std::uint32_t height, int gutter,
                                               double scale) {
    for (const Sheet& made : sheets_)
        if (made.width == width && made.height == height && made.gutter == gutter)
            return made.pixels;
    // The Pocket Bible's paper: India (P001), quiet (P012), a soft shadow into the
    // gutter (P003). Made once for each page size and side.
    paper::Parameters stock = paper::preset(paper::Preset::india);
    stock.pixels_per_point = std::max(0.5, scale);
    stock.seed = gutter < 0 ? 1611 : 1612;
    paper::Shading shading;
    shading.gutter = gutter < 0 ? paper::Gutter::left : (gutter > 0 ? paper::Gutter::right : paper::Gutter::none);
    shading.edge_width = 6;
    paper::Image image;
    Sheet made;
    made.width = width;
    made.height = height;
    made.gutter = gutter;
    if (paper::generate_sheet(stock, static_cast<int>(width), static_cast<int>(height), shading, image)) {
        made.pixels.resize(image.rgba.size());
        for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
            made.pixels[i] = image.rgba[i + 2];
            made.pixels[i + 1] = image.rgba[i + 1];
            made.pixels[i + 2] = image.rgba[i];
            made.pixels[i + 3] = 255;
        }
    }
    if (sheets_.size() >= 4)
        sheets_.erase(sheets_.begin());
    sheets_.push_back(std::move(made));
    return sheets_.back().pixels;
}

void Reader::draw(Canvas& canvas, double scale, Printer& printer, const WoodTiles& wood) {
    fill_with_tile(canvas, wood.wall, wood.wall_width, wood.wall_height, 0, 0, canvas.w, canvas.h, wall_shade);
    if (work_ == nullptr)
        return;
    const int margin = static_cast<int>(std::lround(std::min(canvas.w, canvas.h) * margin_fraction));
    const int inner_width = std::max(1, canvas.w - 2 * margin);
    const int inner_height = std::max(1, canvas.h - 2 * margin);
    geometry_ = page::fit_spread(static_cast<std::uint32_t>(inner_width), static_cast<std::uint32_t>(inner_height));
    geometry_.left.x += margin;
    geometry_.left.y += margin;
    geometry_.right.x += margin;
    geometry_.right.y += margin;

    // The book's own shadow on the table.
    const page::PageRect& first = geometry_.left;
    const double spread_width = static_cast<double>(first.width) * (geometry_.folded ? 1 : 2);
    canvas.save();
    canvas.set_transform(Mat{});
    for (int ring = 6; ring >= 1; --ring) {
        const double grow = ring * 2.0 * std::max(1.0, scale);
        canvas.begin();
        canvas.rrect(first.x - grow + 2 * scale, first.y - grow + 4 * scale, spread_width + 2 * grow,
                     first.height + 2 * grow, grow);
        canvas.fill(rgb(5, 3, 2, 0.07f));
    }
    canvas.restore();

    if (contents_)
        lay_out_contents(printer);
    else
        ensure_laid_out(printer);
    const std::vector<page::PageLayout>& shown = contents_ ? contents_pages_ : pages_;
    const std::size_t first_page = contents_ ? contents_page_ : page_;
    const page::PixelTarget target{canvas.px.data(), static_cast<std::size_t>(canvas.w) * 4,
                                   static_cast<std::uint32_t>(canvas.w), static_cast<std::uint32_t>(canvas.h)};
    const page::PageLayout blank;
    const int sides = geometry_.folded ? 1 : 2;
    for (int side = 0; side < sides; ++side) {
        const page::PageRect& rect = side == 0 ? geometry_.left : geometry_.right;
        const int gutter = geometry_.folded ? -1 : (side == 0 ? 1 : -1);
        const std::vector<std::uint8_t>& paper_pixels = sheet(rect.width, rect.height, gutter, scale);
        const page::PaperImage paper{paper_pixels.empty() ? nullptr : paper_pixels.data(),
                                     static_cast<std::size_t>(rect.width) * 4, rect.width, rect.height};
        const std::size_t index = first_page + static_cast<std::size_t>(side);
        const page::PageLayout& layout = index < shown.size() && printer.ready() ? shown[index] : blank;
        printer.rasterizer().draw_page(layout, target, rect, paper);
    }
}

}  // namespace pl
