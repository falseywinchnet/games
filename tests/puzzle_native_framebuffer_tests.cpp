#include "collection.hpp"
#include "gui_forms/application.hpp"
#include "puzzle_view.hpp"
#include "runtime_paths.hpp"
#include "test_paths.hpp"
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace games {
namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::uint32_t bgra(std::uint32_t pixel, gf::FramebufferChannelOrder order) {
    return 0xff000000U |
           (order == gf::FramebufferChannelOrder::rgba
                ? ((pixel & 0xffU) << 16) | (pixel & 0xff00U) | ((pixel >> 16) & 0xffU)
                : pixel);
}
// A surface pixel read back as BGRA, whichever order the surface was written in.
std::uint32_t surface_bgra(std::uint32_t pixel, gf::LiveSurfacePixelFormat format) {
    return format == gf::LiveSurfacePixelFormat::rgba32_premultiplied_srgb
               ? (pixel & 0xff00ff00U) | ((pixel & 0xffU) << 16) | ((pixel >> 16) & 0xffU)
               : pixel;
}
} // namespace
struct PuzzleFramebufferTest {
    static bool compare(PuzzleView& view, gf::Window& window) {
        const gf::Rect bounds = view.client_rectangle();
        std::unique_ptr<gf::PaintFramebuffer> reference =
            window.create_framebuffer({bounds.width, bounds.height}, view.framebuffer_scale_);
        require(reference && (*reference).begin(view.frame_images_, bounds),
                "Native reference framebuffer");
        gf::Painter& painter = (*reference).painter();
        view.framebuffer_painting_ = true;
        painter.fill_rect(bounds, gf::Color::rgba(112, 126, 133));
        if (view.game.kind == PuzzleKind::gems)
            view.paint_gems(painter);
        else
            view.paint_untangle(painter);
        view.text(painter, 17, bounds.height - 11, view.game.message, 13,
                  gf::Color::rgba(0, 0, 0, 120));
        view.text(painter, 16, bounds.height - 12, view.game.message, 13,
                  gf::Color::rgba(231, 238, 247));
        view.framebuffer_painting_ = false;
        (*reference).end();
        const gf::LiveSurfaceFrame actual = (*view.surface_).acquire_latest();
        require(actual.width() == (*reference).width() && actual.height() == (*reference).height(),
                "Device-sized direct surface");
        std::size_t mismatches = 0;
        std::size_t rounding_pixels = 0;
        for (std::uint32_t y = 0; y < actual.height(); ++y) {
            const std::uint32_t* expected = reinterpret_cast<const std::uint32_t*>(
                (*reference).pixels().data() + y * (*reference).row_bytes());
            const std::uint32_t* observed = reinterpret_cast<const std::uint32_t*>(
                actual.pixels().data() + y * actual.row_bytes());
            for (std::uint32_t x = 0; x < actual.width(); ++x)
                if (surface_bgra(observed[x], actual.pixel_format()) != bgra(expected[x], (*reference).channel_order())) {
                    const std::uint32_t wanted = bgra(expected[x], (*reference).channel_order());
                    bool rounding = (*reference).channel_order() == gf::FramebufferChannelOrder::rgba;
                    for (unsigned shift = 0; shift < 32; shift += 8)
                        rounding = rounding &&
                                   std::abs(static_cast<int>((surface_bgra(observed[x], actual.pixel_format()) >> shift) & 255U) -
                                            static_cast<int>((wanted >> shift) & 255U)) <= 1;
                    // Skia's clipped SIMD spans can round an 8-bit blend one level
                    // differently. Bound both the channel error and pixel count;
                    // a stale edge, damaged shape or broad drift must still fail.
                    if (rounding) {
                        ++rounding_pixels;
                        continue;
                    }
                    if (mismatches < 8)
                        std::cerr << "pixel " << x << ',' << y << " expected=" << std::hex
                                  << bgra(expected[x], (*reference).channel_order())
                                  << " observed=" << surface_bgra(observed[x], actual.pixel_format()) << std::dec << '\n';
                    ++mismatches;
                }
        }
        if (mismatches)
            std::cerr << puzzle_slug(view.game.kind) << " reference mismatch pixels=" << mismatches
                      << " scale=" << view.framebuffer_scale_ << " hover=" << view.hover_
                      << " size=" << bounds.width << 'x' << bounds.height << '\n';
        const std::uint64_t pixel_count = static_cast<std::uint64_t>(actual.width()) * actual.height();
        const std::uint64_t rounding_limit = std::max<std::uint64_t>(1, pixel_count / 10000U);
        if (rounding_pixels)
            std::cout << "Skia one-level blend rounding pixels=" << rounding_pixels
                      << " of " << pixel_count << " scale=" << view.framebuffer_scale_
                      << " hover=" << view.hover_ << '\n';
        const bool matches = mismatches == 0 && rounding_pixels <= rounding_limit;
        return matches;
    }
    static void run(PuzzleView& view, gf::Window& window) {
        std::cout << "Native framebuffer renderer: " << window.metrics().snapshot().renderer_name
                  << "; text provider=" << (window.text_metrics_provider() != nullptr) << '\n';
        require(!window.create_framebuffer({1, 1}, std::numeric_limits<double>::infinity()),
                "Invalid scale rejected");
        view.game.deal(42);
        view.reduced_ = true;
        view.game.message = "Framebuffer reference fixture";
        if (view.game.kind == PuzzleKind::untangle) {
            view.game.state.nodes = {{.15, .2}, {.5, .2}, {.85, .2}};
            view.game.state.edges = {{0, 1}, {1, 2}};
            view.game.state.marks.fill(0);
            view.game.state.aux.fill(0);
        }
        bool pixels_match = true;
        for (const double scale : {1.0, 1.25, 2.0}) {
            window.set_scale(scale);
            view.arrange({0, 0, scale == 1.25 ? 600.0 : 1060.0, scale == 1.25 ? 420.0 : 680.0});
            view.refresh_scene();
            view.render();
            view.present_framebuffer(view.client_rectangle());
            require(view.surface_ && view.direct_,
                    "Puzzle registered an actual direct live surface");
            pixels_match = compare(view, window) && pixels_match;
            const gf::LiveSurfaceFrame held = (*view.surface_).acquire_latest();
            const std::vector<std::byte> held_pixels(held.pixels().begin(), held.pixels().end());
            const std::vector<std::byte> background = view.static_pixels_;
            // Hover changes heads/highlights but neither scenery nor yarn.
            view.hover_ = 0;
            view.invalidate_animation(
                view.game.kind == PuzzleKind::gems ? view.board_ : view.untangle_board_damage());
            require(background == view.static_pixels_, "Animation reuses static pixels");
            pixels_match = compare(view, window) && pixels_match;
            view.hover_ = -1;
            view.invalidate_animation(
                view.game.kind == PuzzleKind::gems ? view.board_ : view.untangle_board_damage());
            pixels_match = compare(view, window) && pixels_match;
            require(std::equal(held_pixels.begin(), held_pixels.end(), held.pixels().begin()),
                    "Published read lease remains immutable during buffer rotation");
            if (view.game.kind == PuzzleKind::untangle) {
                view.game.state.nodes[0] = {.035, .035};
                view.refresh_scene();
                pixels_match = compare(view, window) && pixels_match;
            }
            const std::uint64_t generations = (*view.surface_).snapshot().published_generation;
            view.set_visible(false);
            view.tick();
            require((*view.surface_).snapshot().published_generation == generations,
                    "Hidden game stops publishing");
            view.set_visible(true);
        }
        require(pixels_match,
                "Cached partial frames match full native redraws within bounded blend rounding");
        std::cout << puzzle_slug(view.game.kind)
                  << ": direct publication, pixel equivalence, partial restore, DPI, resize, "
                     "immutable frames and hidden lifecycle passed\n";
    }
};
struct FramebufferReady {
    std::shared_ptr<Collection> collection;
    void operator()(gf::Window& window, gf::ApplicationWindowHandle handle) const {
        for (const Entry entry : {Entry::gems, Entry::untangle}) {
            (*collection).open_entry(entry);
            window.perform_layout();
            const std::shared_ptr<gf::Control> control =
                window.find(entry == Entry::gems ? "collection.puzzle.0" : "collection.puzzle.2");
            PuzzleView* view = dynamic_cast<PuzzleView*>(control.get());
            require(view != nullptr, "Puzzle attached to real native window");
            PuzzleFramebufferTest::run(*view, window);
        }
        static_cast<void>(handle.request_close());
    }
};
} // namespace games
int main(int argc, char** argv) {
    try {
        if (argc < 1)
            return 1;
        const std::filesystem::path scratch =
            games_test::scratch_directory("playsuite-framebuffer-");
        games_test::isolate_saves(scratch);
        games::initialize_assets(argv[0]);
        gui_forms::Window headless(
            gui_forms::make_control<gui_forms::Control>(gui_forms::StableId("headless")),
            gui_forms::Size{100, 100});
        games::require(!headless.create_framebuffer({100, 100}, 1),
                       "A headless window explicitly reports no native framebuffer");
        std::shared_ptr<games::Collection> collection =
            gui_forms::make_control<games::Collection>(gui_forms::StableId("fixture"));
        gui_forms::ApplicationWindowOptions options;
        options.title = "PlaySuite native framebuffer test";
        options.ready = games::FramebufferReady{collection};
        gui_forms::ApplicationResult result = gui_forms::Application::run(
            std::make_unique<gui_forms::Window>(collection, gui_forms::Size{1060, 680}), options);
        if (result.callback_exception)
            std::rethrow_exception(result.callback_exception);
        if (!result.accepted())
            throw std::runtime_error("Native framebuffer host failed");
        std::filesystem::remove_all(scratch);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
