// The stones engine: every style grows a solid whose turns can be told apart, the same
// seed grows the same solid, every material prints, and a drawn stone covers pixels and
// shades the wood round it.
#include "minerals.hpp"
#include "shapes.hpp"
#include "stone_render.hpp"
#include "tray.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++failures;
}

}  // namespace

int main() {
    for (int s = 0; s < stones::style_count; ++s) {
        const stones::Style style = static_cast<stones::Style>(s);
        const stones::Shape a = stones::Shape::make(4242U + static_cast<std::uint32_t>(s), style);
        const stones::Shape b = stones::Shape::make(4242U + static_cast<std::uint32_t>(s), style);
        std::printf("%-16s %zu vertices, least distance between its turns %.3f\n", stones::style_name(style),
                    a.vertices().size(), stones::self_distance(a, 24));
        check(!a.vertices().empty() && !a.triangles().empty(), "a style grows a solid");
        check(a.vertices().size() == b.vertices().size() && a.seen(0, 16) == b.seen(0, 16), "a seed grows the same solid");
        check(stones::self_distance(a, 24) > 0, "its turns can be told apart");
    }
    const stones::Shape shape = stones::Shape::make(91, stones::Style::flake);
    for (int m = 0; m < stones::mineral_count; ++m) {
        const stones::Finish finish = stones::make_finish(static_cast<stones::Mineral>(m), 7);
        stones::MineralPicture picture;
        stones::print_mineral(finish, shape, 32, picture);
        bool coloured = false;
        for (std::uint32_t c : picture.colour)
            coloured = coloured || (c & 0xffffffU) != 0;
        check(picture.side == 32 && coloured, stones::mineral_name(static_cast<stones::Mineral>(m)));
    }
    // A stone drawn: some pixels its own, some the shade it throws on the wood.
    const stones::Finish finish = stones::make_finish(stones::Mineral::jade, 3);
    stones::MineralPicture print;
    stones::print_mineral(finish, shape, 64, print);
    stones::StoneWork work;
    stones::DrawStone how;
    how.radius = 40;
    stones::StonePicture picture;
    stones::draw_stone(stones::StoneLook{&shape, &finish, &print}, stones::orientation_pose(0), how, work, picture);
    int covered = 0;
    int shaded = 0;
    for (std::size_t i = 0; i < picture.add.size(); ++i) {
        if ((picture.add[i] >> 24) > 0)
            ++covered;
        else if ((picture.keep[i] & 0xffU) < 250)
            ++shaded;
    }
    check(covered > 1000, "a drawn stone covers its pixels");
    check(shaded > 100, "and shades the wood round it");
    // Its shade fades out before the picture's edge (the picture is cropped to what it
    // changes, so its top row holds only the faintest of it).
    bool border_clear = true;
    for (int x = 0; x < picture.width; ++x)
        border_clear = border_clear && (picture.keep[static_cast<std::size_t>(x)] & 0xffU) >= 240;
    check(border_clear, "the shade is gone by the picture's edge");
    // The tray, drawn in each wood.
    for (int wood = 0; wood < 4; ++wood) {
        stones::TrayPlan plan;
        plan.width = 96;
        plan.height = 96;
        plan.cell = 32;
        plan.columns = 3;
        plan.rows = 3;
        plan.wood = wood;
        std::vector<std::uint32_t> pixels(96 * 96, 0U);
        stones::draw_tray(plan, pixels.data(), 96, true, 0, 96);
        bool drawn = true;
        for (std::uint32_t p : pixels)
            drawn = drawn && (p >> 24) == 0xffU;
        check(drawn, "the tray draws every pixel");
    }
    std::printf("%d checks failed\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
