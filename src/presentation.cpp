#include "presentation.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace games {
static gf::Color mix(gf::Color a, gf::Color b, double t) {
    return gf::Color::rgba(static_cast<unsigned char>(a.red * (1 - t) + b.red * t),
                           static_cast<unsigned char>(a.green * (1 - t) + b.green * t),
                           static_cast<unsigned char>(a.blue * (1 - t) + b.blue * t),
                           static_cast<unsigned char>(a.alpha * (1 - t) + b.alpha * t));
}
static void gradient(gf::Painter& p, gf::Rect r, gf::Color a, gf::Color b) {
    const gf::GradientStop stops[] = {{0, a}, {1, b}};
    p.fill_linear_gradient(r, {r.x, r.y}, {r.x, r.y + r.height}, stops);
}
gf::Color game_accent(int game) {
    const gf::Color colors[] = {gf::Color::rgba(213, 184, 113), gf::Color::rgba(125, 191, 238),
                                gf::Color::rgba(191, 145, 231), gf::Color::rgba(148, 205, 171),
                                gf::Color::rgba(119, 212, 221), gf::Color::rgba(129, 182, 225),
                                gf::Color::rgba(228, 172, 121), gf::Color::rgba(167, 219, 181),
                                gf::Color::rgba(243, 203, 100), gf::Color::rgba(192, 198, 142)};
    return colors[std::clamp(game, 0, 9)];
}
GameButton::GameButton(gf::StableId id, std::string label)
    : Button(std::move(id), std::move(label)) {
    set_font({gf::FontRole::content, 14, 600, false, .1});
    set_use_mnemonic(false);
    set_content_padding({9, 2, 9, 2});
}
void GameButton::set_skin(ButtonSkin skin) {
    skin_ = skin;
    invalidate(gf::Dirty::paint);
}
void GameButton::on_paint(gf::Painter& p, gf::Rect) {
    gf::Rect b = client_rectangle();
    gf::Rect r{1, 1, b.width - 2, b.height - 3};
    bool down = pressed_visual(), hot = hovered_visual(), chosen = selected(),
         light = skin_ == ButtonSkin::ivory || skin_ == ButtonSkin::blue;
    gf::Color top = light ? gf::Color::rgba(247, 246, 235) : gf::Color::rgba(56, 79, 99),
              bottom = light ? gf::Color::rgba(212, 218, 202) : gf::Color::rgba(30, 49, 69),
              border = light ? gf::Color::rgba(119, 145, 136) : gf::Color::rgba(82, 107, 126),
              ink = light ? gf::Color::rgba(39, 65, 60) : gf::Color::rgba(226, 236, 242);
    if (skin_ == ButtonSkin::blue) {
        top = gf::Color::rgba(244, 251, 255);
        bottom = gf::Color::rgba(197, 223, 241);
        border = gf::Color::rgba(113, 157, 191);
        ink = gf::Color::rgba(31, 81, 120);
    }
    if (skin_ == ButtonSkin::mint) {
        top = gf::Color::rgba(77, 109, 102);
        bottom = gf::Color::rgba(39, 68, 66);
        border = gf::Color::rgba(125, 161, 137);
    }
    if (chosen) {
        top = gf::Color::rgba(235, 212, 153);
        bottom = gf::Color::rgba(189, 150, 82);
        border = gf::Color::rgba(130, 100, 49);
        ink = gf::Color::rgba(45, 49, 49);
    }
    if (hot) {
        top = mix(top, gf::Color::rgba(245, 246, 229), .12);
        border = mix(border, gf::Color::rgba(181, 220, 225), .55);
    }
    if (down)
        std::swap(top, bottom);
    if (!enabled()) {
        top = mix(top, bottom, .5);
        ink = mix(ink, bottom, .55);
        border = mix(border, bottom, .4);
    }
    if (!down)
        p.draw_box_shadow(r, 5, {0, 2}, 2, 0, gf::Color::rgba(0, 8, 17, 70));
    p.save();
    p.clip_rounded_rect(r, 5);
    gradient(p, r, top, bottom);
    p.restore();
    p.stroke_rounded_rect(r, 5, border, 1);
    p.draw_line({r.x + 5, r.y + 1}, {r.x + r.width - 5, r.y + 1},
                gf::Color::rgba(255, 255, 255,
                                down    ? 20
                                : light ? 195
                                        : 53),
                1);
    p.draw_line({r.x + 4, r.y + r.height - 1}, {r.x + r.width - 4, r.y + r.height - 1},
                gf::Color::rgba(6, 20, 35, light ? 30 : 90), 1);
    paint_button_content(p, r, text(), ink, {down ? 1.0 : 0, down ? 1.0 : 0}, chosen);
    if (focus_cue_visible())
        p.stroke_rounded_rect(
            {4, 4, b.width - 8, b.height - 9}, 3,
            light ? gf::Color::rgba(45, 103, 131) : gf::Color::rgba(170, 216, 233), 1);
}
std::shared_ptr<const gf::Theme> games_theme(ButtonSkin skin) {
    gf::ThemeDefinition theme = gf::windows_professional_theme_definition();
    theme.id = "games-authored-" + std::to_string(static_cast<int>(skin));
    bool light = skin == ButtonSkin::blue || skin == ButtonSkin::ivory;
    theme.structure.typography.control = {gf::FontRole::content, 14, 400, false};
    theme.structure.typography.field = {gf::FontRole::content, 16, 400, false};
    theme.compatibility.text = light ? gf::Color::rgba(35, 63, 82) : gf::Color::rgba(229, 235, 241);
    theme.compatibility.border =
        light ? gf::Color::rgba(119, 156, 181) : gf::Color::rgba(87, 117, 138);
    for (std::size_t i = 0; i < gf::control_surface_state_count; ++i) {
        gf::ControlVisualRecipe& editor =
            theme.roles[static_cast<std::size_t>(gf::ControlVisualRole::editor)].ordinary[i];
        editor.material.fills = {gf::MaterialFillLayer::solid(light ? gf::Color::rgba(252, 253, 250)
                                                                    : gf::Color::rgba(12, 27, 43))};
        editor.material.corner_radius = 4;
        editor.material.border = gf::MaterialBorder{
            light ? gf::Color::rgba(115, 153, 184) : gf::Color::rgba(104, 147, 171), 1};
        editor.text = light ? gf::Color::rgba(26, 59, 83) : gf::Color::rgba(239, 240, 222);
        editor.focus_ring = gf::Color::rgba(140, 192, 222);
    }
    return gf::Theme::create(std::move(theme));
}
void paint_polygon(gf::Painter& p, const std::vector<gf::Point>& points, gf::Color color) {
    double ymin = 100000, ymax = -100000;
    for (gf::Point q : points) {
        ymin = std::min(ymin, q.y);
        ymax = std::max(ymax, q.y);
    }
    for (double y = std::ceil(ymin); y < ymax; ++y) {
        std::vector<double> intersections;
        for (std::size_t i = 0; i < points.size(); ++i) {
            gf::Point a = points[i], b = points[(i + 1) % points.size()];
            if ((a.y <= y && b.y > y) || (b.y <= y && a.y > y))
                intersections.push_back(a.x + (y - a.y) * (b.x - a.x) / (b.y - a.y));
        }
        std::sort(intersections.begin(), intersections.end());
        for (std::size_t i = 1; i < intersections.size(); i += 2)
            p.draw_line({intersections[i - 1], y}, {intersections[i], y}, color, 1);
    }
}
void paint_emblem(gf::Painter& p, gf::Rect r, int game) {
    double x = r.x, y = r.y, w = r.width, h = r.height;
    gf::Color accent = game_accent(game);
    gf::Color white = gf::Color::rgba(235, 241, 244);
    if (game == 0) {
        p.fill_rounded_rect({x + w * .12, y + h * .2, w * .48, h * .68}, 4,
                            gf::Color::rgba(132, 154, 171));
        p.fill_rounded_rect({x + w * .31, y + h * .08, w * .5, h * .68}, 4,
                            gf::Color::rgba(248, 243, 221));
        p.draw_text_utf8({x + w * .42, y + h * .57}, "A",
                         {gf::FontRole::content, h * .40, 700, false},
                         gf::Color::rgba(147, 47, 70));
    } else if (game == 1) {
        p.fill_rounded_rect({x + w * .12, y + h * .1, w * .77, h * .77}, 4,
                            gf::Color::rgba(223, 235, 244));
        for (int i = 1; i < 3; ++i) {
            p.draw_line({x + w * (.12 + i * .257), y + h * .1},
                        {x + w * (.12 + i * .257), y + h * .87}, gf::Color::rgba(108, 158, 192), 1);
            p.draw_line({x + w * .12, y + h * (.1 + i * .257)},
                        {x + w * .89, y + h * (.1 + i * .257)}, gf::Color::rgba(108, 158, 192), 1);
        }
        p.draw_text_utf8({x + w * .20, y + h * .56}, "7",
                         {gf::FontRole::content, h * .25, 700, false},
                         gf::Color::rgba(41, 95, 147));
        p.draw_text_utf8({x + w * .70, y + h * .80}, "3",
                         {gf::FontRole::content, h * .25, 700, false},
                         gf::Color::rgba(41, 95, 147));
    } else if (game == 2) {
        paint_polygon(p,
                      {{x + w * .13, y + h * .37},
                       {x + w * .3, y + h * .13},
                       {x + w * .71, y + h * .13},
                       {x + w * .89, y + h * .37},
                       {x + w * .5, y + h * .9}},
                      gf::Color::rgba(133, 80, 181));
        paint_polygon(
            p, {{x + w * .13, y + h * .37}, {x + w * .89, y + h * .37}, {x + w * .5, y + h * .9}},
            accent);
        paint_polygon(
            p, {{x + w * .34, y + h * .37}, {x + w * .65, y + h * .37}, {x + w * .5, y + h * .9}},
            gf::Color::rgba(226, 181, 255));
        p.draw_line({x + w * .3, y + h * .15}, {x + w * .71, y + h * .15},
                    gf::Color::rgba(255, 233, 253), 2);
    } else if (game == 3) {
        paint_polygon(p,
                      {{x + w * .15, y + h * .28},
                       {x + w * .5, y + h * .10},
                       {x + w * .85, y + h * .28},
                       {x + w * .5, y + h * .47}},
                      gf::Color::rgba(203, 230, 210));
        paint_polygon(p,
                      {{x + w * .15, y + h * .28},
                       {x + w * .5, y + h * .47},
                       {x + w * .5, y + h * .88},
                       {x + w * .15, y + h * .68}},
                      gf::Color::rgba(118, 167, 152));
        paint_polygon(p,
                      {{x + w * .5, y + h * .47},
                       {x + w * .85, y + h * .28},
                       {x + w * .85, y + h * .68},
                       {x + w * .5, y + h * .88}},
                      gf::Color::rgba(155, 199, 185));
    } else if (game == 4) {
        gf::Point points[] = {{x + w * .18, y + h * .23},
                              {x + w * .82, y + h * .24},
                              {x + w * .32, y + h * .80},
                              {x + w * .77, y + h * .75}};
        for (int i = 0; i < 4; ++i)
            p.draw_line(points[i], points[(i + 1) % 4], accent, 2);
        p.draw_line(points[0], points[3], accent, 2);
        for (gf::Point q : points)
            p.fill_rounded_rect({q.x - 4, q.y - 4, 8, 8}, 4, white);
    } else if (game == 5) {
        for (int direction = 0; direction < 3; ++direction) {
            gf::Point previous{};
            for (int k = 0; k <= 40; ++k) {
                double a = k * 6.2831853 / 40, u = std::cos(a) * w * .4, v = std::sin(a) * h * .16,
                       turn = direction * 1.0471976;
                gf::Point q{x + w * .5 + u * std::cos(turn) - v * std::sin(turn),
                            y + h * .5 + u * std::sin(turn) + v * std::cos(turn)};
                if (k)
                    p.draw_line(previous, q, accent, 1.2);
                previous = q;
            }
        }
        p.fill_rounded_rect({x + w * .43, y + h * .43, w * .14, h * .14}, w * .07, white);
    } else if (game == 6) {
        const gf::Color colors[] = {gf::Color::rgba(224, 115, 112), gf::Color::rgba(100, 184, 224),
                                    gf::Color::rgba(231, 187, 98), gf::Color::rgba(141, 203, 161)};
        for (int i = 0; i < 4; ++i)
            p.fill_rounded_rect(
                {x + w * (.14 + (i % 2) * .4), y + h * (.14 + (i / 2) * .4), w * .29, h * .29},
                w * .145, colors[i]);
    } else if (game == 7) {
        p.fill_rounded_rect({x + w * .1, y + h * .3, w * .8, h * .5}, 5,
                            gf::Color::rgba(235, 153, 187));
        p.stroke_rounded_rect({x + w * .1, y + h * .3, w * .8, h * .5}, 5,
                              gf::Color::rgba(255, 210, 231), 1);
        for (int i = 0; i < 6; ++i) {
            p.fill_rounded_rect({x + w * (.16 + i * .12), y + h * .43, w * .08, h * .20}, 2,
                                gf::Color::rgba(31, 54, 58));
            p.fill_rounded_rect(
                {x + w * (.17 + i * .12), y + h * (i == 0 ? .44 : .54), w * .06, h * .06}, 1,
                accent);
        }
    } else if (game == 8) {
        paint_polygon(
            p, {{x + w * .1, y + h * .1}, {x + w * .88, y + h * .1}, {x + w * .49, y + h * .49}},
            gf::Color::rgba(239, 197, 89));
        paint_polygon(
            p, {{x + w * .1, y + h * .1}, {x + w * .49, y + h * .49}, {x + w * .1, y + h * .88}},
            gf::Color::rgba(86, 153, 191));
        paint_polygon(
            p, {{x + w * .53, y + h * .52}, {x + w * .9, y + h * .18}, {x + w * .9, y + h * .87}},
            gf::Color::rgba(239, 197, 89));
        paint_polygon(
            p, {{x + w * .14, y + h * .9}, {x + w * .52, y + h * .54}, {x + w * .9, y + h * .9}},
            gf::Color::rgba(86, 153, 191));
    } else {
        p.fill_rounded_rect({x + w * .12, y + h * .41, w * .69, h * .45}, h * .2,
                            gf::Color::rgba(237, 199, 91));
        p.fill_rounded_rect({x + w * .44, y + h * .19, w * .37, h * .4}, h * .17,
                            gf::Color::rgba(251, 221, 125));
        paint_polygon(
            p, {{x + w * .76, y + h * .37}, {x + w * .99, y + h * .44}, {x + w * .76, y + h * .51}},
            gf::Color::rgba(194, 119, 51));
        p.fill_rounded_rect({x + w * .43, y + h * .13, w * .39, h * .15}, h * .06,
                            gf::Color::rgba(132, 104, 64));
        p.draw_line({x + w * .62, y + h * .13}, {x + w * .64, y + h * .04},
                    gf::Color::rgba(97, 81, 57), 3);
        p.fill_rounded_rect({x + w * .67, y + h * .31, w * .055, h * .055}, h * .025,
                            gf::Color::rgba(38, 53, 57));
        p.draw_line({x + w * .3, y + h * .86}, {x + w * .26, y + h * .97},
                    gf::Color::rgba(189, 117, 53), 3);
        p.draw_line({x + w * .58, y + h * .84}, {x + w * .65, y + h * .96},
                    gf::Color::rgba(189, 117, 53), 3);
    }
}
const char* collection_title(int game) {
    const char* names[] = {"Card tables", "Sudoku",    "Gems",      "Nature Cube",  "Untangle",
                           "Atom Probe",  "Four Pegs", "Switchbox", "Puzzle Solve", "Eggy"};
    return names[std::clamp(game, 0, 9)];
}
GameTile::GameTile(gf::StableId id, int game) : Button(std::move(id)), game_(game) {
    set_text(collection_title(game));
    set_accessible_name(collection_title(game));
    set_use_mnemonic(false);
}
void GameTile::on_pointer(gf::PointerEvent& e) {
    if (e.action == gf::PointerAction::down)
        open_requested_ = e.click_count >= 2;
    gf::Button::on_pointer(e);
}
void GameTile::on_key(gf::KeyEvent& e) {
    open_requested_ = true;
    gf::Button::on_key(e);
}
void GameTile::on_paint(gf::Painter& p, gf::Rect) {
    gf::Rect b = client_rectangle(), r{2, 2, b.width - 4, b.height - 4};
    if (selected() || hovered_visual()) {
        p.fill_rect(r,
                    selected() ? gf::Color::rgba(217, 233, 243) : gf::Color::rgba(240, 245, 246));
        p.stroke_rect(
            r, selected() ? gf::Color::rgba(132, 170, 192) : gf::Color::rgba(213, 225, 230), 1);
    }
    paint_emblem(p, {b.width / 2 - 28, 14, 56, 56}, game_);
    gf::FontSpec f{gf::FontRole::content, 15, selected() ? std::uint16_t(600) : std::uint16_t(400),
                   false};
    auto m = p.measure_text_utf8(text(), f);
    p.draw_text_utf8({(b.width - m.width) / 2, 91}, text(), f, gf::Color::rgba(47, 65, 78));
    const char* types[] = {"4 card games",   "Number logic",    "Match three",  "Path puzzle",
                           "Graph puzzle",   "Deduction",       "Codebreaking", "",
                           "Reconstruction", "An endless climb"};
    gf::FontSpec small{gf::FontRole::content, 12, 400, false};
    auto sm = p.measure_text_utf8(types[game_], small);
    p.draw_text_utf8({(b.width - sm.width) / 2, 111}, types[game_], small,
                     gf::Color::rgba(113, 124, 132));
    if (focus_cue_visible())
        p.stroke_rect({5, 5, b.width - 10, b.height - 10}, gf::Color::rgba(84, 133, 169), 1);
}
LibrarySurface::LibrarySurface(gf::StableId id) : Control(std::move(id)) {}
void LibrarySurface::arrange(gf::Rect b) {
    arrange_self(b);
    const double field = b.width - 470, tw = field / 3;
    const int entries = category == 0 ? 10 : category == 2 ? 8 : 1;
    const int rows = (entries + 2) / 3;
    const double row_pitch = std::min(146.0, (b.height - 190) / rows);
    int index = 0;
    for (const auto& child : children()) {
        if (auto tile = std::dynamic_pointer_cast<GameTile>(child)) {
            int g = tile->game_index();
            bool shown = category == 0 || (category == 1 && g == 0) ||
                         (category == 2 && g > 0 && g < 9) || (category == 3 && g == 9);
            tile->set_visible(shown);
            if (shown) {
                set_child_layout(child,
                                 {202 + (index % 3) * tw, 175.0 + (index / 3) * row_pitch, tw - 10, row_pitch - 10});
                ++index;
            }
        } else {
            std::string id(child->stable_id().value());
            if (id == "collection.open")
                set_child_layout(child, {b.width - 234, b.height - 110, 204, 35});
            else if (id.find("collection.category.") == 0) {
                int c = std::stoi(id.substr(20));
                set_child_layout(child, {20, 188 + c * 40.0, 156, 31});
            }
        }
    }
}
void LibrarySurface::on_paint(gf::Painter& p, gf::Rect) {
    const gf::Rect b = client_rectangle();
    const gf::Color ink = gf::Color::rgba(48, 65, 77), muted = gf::Color::rgba(107, 121, 130),
                    line = gf::Color::rgba(179, 192, 196);
    p.fill_rect(b, gf::Color::rgba(128, 140, 148));
    const gf::GradientStop wash[] = {{0, gf::Color::rgba(219, 235, 231)},
                                     {.48, gf::Color::rgba(236, 237, 226)},
                                     {1, gf::Color::rgba(218, 229, 241)}};
    p.fill_linear_gradient({0, 0, b.width, 83}, {0, 0}, {b.width, 83}, wash);
    // Quiet translucent washes suggest the house fresco without a decorative card wall.
    for (int i = 0; i < 9; ++i)
        p.fill_rounded_rect({b.width - 550 + i * 61.0, -31 + (i % 3) * 19.0, 170, 100}, 48,
                            gf::Color::rgba(160 + i * 5, 190 + i * 3, 191 + i * 4, 18));
    p.draw_text_utf8({26, 37}, "The game collection", {gf::FontRole::control, 28, 600, false}, ink);
    p.draw_text_utf8({27, 62}, "Cards, puzzles, and a very tall mountain.",
                     {gf::FontRole::content, 15, 400, false}, muted);
    p.fill_rect({0, 84, b.width, 31}, gf::Color::rgba(244, 245, 241));
    p.draw_text_utf8({25, 105}, "COLLECTION", {gf::FontRole::content, 11, 700, false, .8}, ink);
    p.draw_text_utf8({195, 105}, "Select a game to inspect it. Double click to play.",
                     {gf::FontRole::content, 13, 400, false}, muted);
    p.fill_rect({0, 116, b.width, 29}, gf::Color::rgba(234, 238, 237));
    const char* groups[] = {"All games", "Card games", "Puzzles", "Long climb"};
    p.draw_text_utf8({25, 136}, std::string("Games  /  ") + groups[category],
                     {gf::FontRole::content, 13, 400, false}, ink);
    p.fill_rect({0, 147, 190, b.height - 178}, gf::Color::rgba(232, 236, 235));
    p.draw_text_utf8({20, 174}, "BROWSE", {gf::FontRole::content, 11, 700, false, 1}, muted);
    p.fill_rect({192, 147, b.width - 458, b.height - 178}, gf::Color::rgba(255, 255, 252));
    p.fill_rect({b.width - 264, 147, 264, b.height - 178}, gf::Color::rgba(241, 240, 233));
    p.draw_text_utf8({b.width - 239, 174}, "SELECTED GAME",
                     {gf::FontRole::content, 11, 700, false, .8}, muted);
    paint_emblem(p, {b.width - 192, 199, 112, 112}, selection);
    p.draw_text_utf8({b.width - 239, 351}, collection_title(selection),
                     {gf::FontRole::control, 25, 600, false}, ink);
    const std::vector<std::vector<std::string>> descriptions = {
        {"Solitaire, FreeCell, Spider,", "and Hearts.", "Four tables. Separate saved games."},
        {"A number puzzle with notes,", "undo, and several difficulties."},
        {"Make matches. Build cascades.", "Watch for special gems."},
        {"Connect pairs across three faces.", "Move the mouse to tilt the cube."},
        {"Reposition the points until", "every crossing is clear."},
        {"Send probes into the chamber.", "Deduce three hidden atoms."},
        {"Face the Curator's secret code.", "Four places. Ten attempts."},
        {"A girl guards six switches.", "Find the order that lights them all.", "Fewer flips make a better score."},
        {"Triangles, square, parallelogram.", "Rotate and reflect the pieces",
         "to reproduce a two-color target."},
        {"Eggy and the Very, Very", "Tall Mountain", "Help a little duckling climb.",
         "He carries on while you are away."}};
    int row = 0;
    for (const auto& t : descriptions[selection])
        p.draw_text_utf8({b.width - 239, 386 + row++ * 24.0}, t,
                         {gf::FontRole::content, 14, 400, false}, muted);
    p.draw_line({b.width - 239, b.height - 145}, {b.width - 30, b.height - 145}, line, 1);
    p.draw_text_utf8({b.width - 239, b.height - 128}, "Progress saves automatically",
                     {gf::FontRole::content, 12, 400, false}, muted);
    p.fill_rect({0, b.height - 29, b.width, 29}, gf::Color::rgba(226, 230, 229));
    p.draw_text_utf8({20, b.height - 10}, "13 games  ·  10 entries",
                     {gf::FontRole::content, 12, 400, false}, ink);
    p.draw_text_utf8({b.width - 260, b.height - 10}, "Rainstar Games",
                     {gf::FontRole::content, 12, 400, false}, muted);
}
void paint_heading(gf::Painter& p, gf::Rect b, const std::string& title, int game) {
    gradient(p, b, gf::Color::rgba(249, 250, 246), gf::Color::rgba(219, 227, 227));
    p.draw_line({b.x, b.y + b.height - 1}, {b.x + b.width, b.y + b.height - 1},
                gf::Color::rgba(75, 96, 114), 1);
    paint_emblem(p, {b.x + 24, b.y + 10, 43, 43}, game);
    p.draw_text_utf8({b.x + 82, b.y + 41}, title, {gf::FontRole::control, 26, 600, false, .15},
                     gf::Color::rgba(45, 68, 82));
    p.draw_line({b.x + 82, b.y + 53}, {b.x + 122, b.y + 53}, game_accent(game), 2);
}
void paint_instrument(gf::Painter& p, gf::Rect r, const std::string& label, gf::Color accent) {
    p.fill_rounded_rect(r, 7, gf::Color::rgba(13, 29, 43, 150));
    p.stroke_rounded_rect(r, 7, gf::Color::rgba(100, 125, 137, 75), 1);
    p.draw_line({r.x + 14, r.y + 38}, {r.x + r.width - 14, r.y + 38},
                gf::Color::rgba(93, 118, 133, 100), 1);
    p.draw_text_utf8({r.x + 15, r.y + 25}, label, {gf::FontRole::content, 11, 700, false, 1.2},
                     accent);
}
void paint_dialog(gf::Painter& p, gf::Rect r, const std::string& title, gf::Color accent) {
    p.draw_box_shadow(r, 10, {0, 12}, 30, 0, gf::Color::rgba(0, 4, 12, 160));
    p.fill_rounded_rect(r, 10, gf::Color::rgba(25, 42, 61));
    p.save();
    p.clip_rounded_rect(r, 10);
    gradient(p, {r.x, r.y, r.width, 61}, gf::Color::rgba(57, 79, 97), gf::Color::rgba(31, 51, 72));
    p.restore();
    p.stroke_rounded_rect(r, 10, gf::Color::rgba(118, 142, 154), 1);
    p.draw_line({r.x + 1, r.y + 61}, {r.x + r.width - 1, r.y + 61}, gf::Color::rgba(15, 30, 47), 1);
    p.draw_text_utf8({r.x + 26, r.y + 41}, title, {gf::FontRole::control, 25, 600, false, .65},
                     gf::Color::rgba(240, 234, 209));
    p.draw_line({r.x + 27, r.y + 53}, {r.x + 71, r.y + 53}, accent, 2);
}
} // namespace games
