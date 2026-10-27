#include "card_press.hpp"

#include "runtime_paths.hpp"
#include "card_backs.hpp"
#include "table_art.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace games {
namespace {
// A card's artwork brought to the paper's size, on any core: a face or back from its PNG, or
// a drawn back (an empty path).
struct ArtWork {
    std::string path;
    int design = -1;
    CardArt* out = nullptr;
    bool made = false;
    int face = -1;  // which face, or -1 for a back
};
struct ArtRound {
    int width = 0, height = 0;
    std::vector<ArtWork> works;
};
void make_art(void* context, int index) noexcept {
    ArtRound& round = *static_cast<ArtRound*>(context);
    ArtWork& work = round.works[static_cast<std::size_t>(index)];
    try {
        CardArt full;
        if (work.path.empty())
            full = card_art_from_bgra(card_art_width, card_art_height, drawn_card_back(work.design));
        else if (!load_card_png(work.path, full))
            return;
        if (full.width == 0)
            return;
        reduce_card_art(full, round.width, round.height, *work.out);
        work.made = (*work.out).width == round.width;
    } catch (...) {
        work.made = false;  // drawn the old way
    }
}
// One card's picture, on any core.
struct Sheet {
    const CardArt* art = nullptr;
    int x = 0, y = 0;
    CardGlint glint;
    std::vector<std::byte> pixels;
};
struct PrintRound {
    const CardPaper* paper = nullptr;
    LightAt light;
    std::vector<Sheet> sheets;
};
void print_sheet(void* context, int index) noexcept {
    PrintRound& round = *static_cast<PrintRound*>(context);
    Sheet& sheet = round.sheets[static_cast<std::size_t>(index)];
    LightAt at = round.light;
    at.x = sheet.x;
    at.y = sheet.y;
    try {
        print_card(*sheet.art, *round.paper, at, sheet.glint, sheet.pixels);
    } catch (...) {
        sheet.pixels.clear();
    }
}
bool fits(const CardArt& art, const CardPaper& paper) {
    return art.width == paper.width && art.height == paper.height;
}
}  // namespace

void CardPress::set_deck(std::vector<std::string> faces, std::vector<std::string> backs) {
    face_paths_ = std::move(faces);
    back_paths_ = std::move(backs);
    faces_.assign(face_paths_.size(), CardArt{});
    backs_.assign(back_paths_.size(), CardArt{});
    face_failed_.assign(face_paths_.size(), false);
    back_failed_.assign(back_paths_.size(), false);
}

bool CardPress::prepare(int width, int height, double scale, const CardFinish& finish) {
    set_glint(finish);
    if (width == paper_.width && height == paper_.height && scale == scale_ && finish.emboss == finish_.emboss &&
        finish.emboss_size == finish_.emboss_size)
        return false;
    finish_ = finish;
    scale_ = scale;
    paper_ = make_card_paper(width, height, scale, 12 * finish.emboss, .8 + 5.2 * finish.emboss_size);
    ++generation_;
    return true;
}

bool CardPress::light(double width, double height, double card_width) {
    if (light_.width > 0 && width == table_width_ && height == table_height_ && card_width == card_width_)
        return false;
    light_ = light_table(width, height, card_width);
    table_width_ = width;
    table_height_ = height;
    card_width_ = card_width;
    ++generation_;
    return true;
}

std::vector<std::vector<std::byte>> CardPress::print(std::span<const PressSheet> sheets) {
    std::vector<std::vector<std::byte>> pictures(sheets.size());
    if (!ready() || sheets.empty())
        return pictures;
    // The artwork they want, brought to the paper's size where it is not yet.
    ArtRound arts{paper_.width, paper_.height, {}};
    std::vector<bool> face_wanted(faces_.size()), back_wanted(backs_.size());
    for (const PressSheet& sheet : sheets) {
        if (sheet.face >= 0) {
            const std::size_t k = static_cast<std::size_t>(sheet.face);
            if (k >= faces_.size() || face_wanted[k] || face_failed_[k] || fits(faces_[k], paper_))
                continue;
            face_wanted[k] = true;
            arts.works.push_back({face_paths_[k], -1, &faces_[k], false, sheet.face});
        } else {
            const std::size_t d = static_cast<std::size_t>(sheet.design);
            if (sheet.design < 0 || d >= backs_.size() || back_wanted[d] || back_failed_[d] || fits(backs_[d], paper_))
                continue;
            back_wanted[d] = true;
            arts.works.push_back({back_paths_[d], sheet.design, &backs_[d], false, -1});
        }
    }
    if (!arts.works.empty())
        run_on_every_core(static_cast<int>(arts.works.size()), make_art, &arts);
    for (const ArtWork& work : arts.works)
        if (!work.made) {
            if (work.face >= 0)
                face_failed_[static_cast<std::size_t>(work.face)] = true;
            else
                back_failed_[static_cast<std::size_t>(work.design)] = true;
        }
    // The pictures, each on its own core.
    PrintRound round{&paper_, {&light_, table_width_ * scale_, table_height_ * scale_, 0, 0}, {}};
    std::vector<std::size_t> printing;
    for (std::size_t i = 0; i < sheets.size(); ++i) {
        const PressSheet& sheet = sheets[i];
        const CardArt* art = nullptr;
        if (sheet.face >= 0 && static_cast<std::size_t>(sheet.face) < faces_.size()) {
            if (fits(faces_[static_cast<std::size_t>(sheet.face)], paper_))
                art = &faces_[static_cast<std::size_t>(sheet.face)];
        } else if (sheet.face < 0 && sheet.design >= 0 && static_cast<std::size_t>(sheet.design) < backs_.size()) {
            if (fits(backs_[static_cast<std::size_t>(sheet.design)], paper_))
                art = &backs_[static_cast<std::size_t>(sheet.design)];
        }
        if (!art)
            continue;
        const CardGlint glint = sheet.glint ? CardGlint{finish_.sparkle, finish_.glare, 1.5 + 6.5 * finish_.grain, sheet.seed}
                                            : CardGlint{};
        round.sheets.push_back({art, sheet.x, sheet.y, glint, {}});
        printing.push_back(i);
    }
    if (round.sheets.empty())
        return pictures;
    run_on_every_core(static_cast<int>(round.sheets.size()), print_sheet, &round);
    for (std::size_t n = 0; n < printing.size(); ++n)
        pictures[printing[n]] = std::move(round.sheets[n].pixels);
    return pictures;
}

std::vector<std::byte> CardPress::shadow(bool lifted, int& width, int& height) {
    width = height = 0;
    if (paper_.width <= 0)
        return {};
    if (shadow_art_.width == 0 && !load_card_png(asset_directory() + "/card_shadow.png", shadow_art_))
        return {};
    // The artwork's shadow is 440 x 584 about a 360 x 504 card, 40 px of it all round; in
    // the hand a card rides higher and its shadow is larger.
    const double w = paper_.width, margin = 40.0 * w / card_art_width_points, grow = lifted ? 1.06 : 1;
    CardArt reduced;
    reduce_card_art(shadow_art_, static_cast<int>(std::lround((w + 2 * margin) * grow)),
                    static_cast<int>(std::lround((w * 1.4 + 2 * margin) * grow)), reduced);
    width = reduced.width;
    height = reduced.height;
    return premultiplied_bgra(reduced);
}

void CardPress::forget() {
    faces_.assign(face_paths_.size(), CardArt{});
    backs_.assign(back_paths_.size(), CardArt{});
    face_failed_.assign(face_paths_.size(), false);
    back_failed_.assign(back_paths_.size(), false);
    paper_ = {};
}

}  // namespace games
