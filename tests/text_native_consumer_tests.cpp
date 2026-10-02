#include "text_requests.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using gui_forms::TextMaskStatus;
void require(const bool condition, const char* const message) {
    if (!condition) { throw std::runtime_error(message); }
}
std::vector<std::byte> read_font(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    require(static_cast<bool>(input), "Open approved provider font");
    const std::streamoff length = input.tellg();
    require(length > 0 && length <= 4 * 1024 * 1024, "Bound font allocation");
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(length));
    require(static_cast<bool>(input), "Read complete font");
    return bytes;
}
struct Fixture final {
    gui_forms::TextMaskService service{};
    gui_forms::EncodedFontLease fonts{};
    std::unique_ptr<gui_forms::TextMaskSession> session{};
    std::unique_ptr<games::TextRequests> requests{};
    explicit Fixture(const std::filesystem::path& directory) {
        const std::vector<std::byte> carlito = read_font(directory / "Carlito-Regular.ttf");
        const std::vector<std::byte> cousine = read_font(directory / "Cousine-Regular.ttf");
        const std::array<gui_forms::PreparedFontSource, 2> sources{{{.encoded = carlito}, {.encoded = cousine}}};
        gui_forms::TextMaskResult result = service.create_font_bank(sources, fonts);
        require(result.status == TextMaskStatus::success, "Register approved encoded font bank");
        result = service.open_session(nullptr, session);
        require(result.status == TextMaskStatus::success, "Open native consumer session");
        requests = std::make_unique<games::TextRequests>(*session, fonts);
    }
    // Waiting is test orchestration only; the production broker never waits.
    gui_forms::TextMaskResult prepare(const gui_forms::TextMaskRequest& input,
        gui_forms::TextMaskLease& output) {
        const std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(10);
        gui_forms::TextMaskResult result = (*requests).request(input, output);
        while (result.status == TextMaskStatus::pending) {
            require(std::chrono::steady_clock::now() < deadline, "Native request completes within test deadline");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            const gui_forms::TextMaskResult polled = (*requests).poll();
            require(polled.status == TextMaskStatus::success, "Poll native completions");
            result = (*requests).request(input, output);
        }
        return result;
    }
};
void check_mask(const gui_forms::TextMaskLease& mask) {
    require(mask.has_value(), "Successful result has an immutable owner");
    const gui_forms::TextMaskMetrics metrics = mask.metrics();
    const std::span<const std::uint8_t> pixels = mask.coverage();
    require(metrics.width_px <= 4096 && metrics.height_px <= 4096 &&
        metrics.stride_bytes >= metrics.width_px, "Mask shape has bounded device dimensions");
    require(metrics.height_px == 0 || metrics.stride_bytes <= pixels.size() / metrics.height_px,
        "Coverage storage contains every declared row");
    std::uint32_t consumed = 0;
    for (const gui_forms::TextMaskLine& line : mask.lines()) {
        require(line.source_begin == consumed && line.source_begin <= line.text_end &&
            line.text_end <= line.consumed_end, "Logical lines cover source monotonically");
        consumed = line.consumed_end;
    }
    require(consumed == mask.source_utf8().size(), "Logical lines retain the entire admitted source");
}
void same_lines(const gui_forms::TextMaskLease& first, const gui_forms::TextMaskLease& second) {
    const std::span<const gui_forms::TextMaskLine> a = first.lines();
    const std::span<const gui_forms::TextMaskLine> b = second.lines();
    require(a.size() == b.size(), "DPI does not change logical line count");
    for (std::size_t index = 0; index < a.size(); ++index) {
        require(a[index].source_begin == b[index].source_begin && a[index].text_end == b[index].text_end &&
            a[index].consumed_end == b[index].consumed_end && a[index].baseline == b[index].baseline &&
            a[index].advance == b[index].advance, "DPI does not change line breaks or logical placement");
    }
}
void write_preview(const gui_forms::TextMaskLease& mask, const std::filesystem::path& path) {
    check_mask(mask);
    const gui_forms::TextMaskMetrics metrics = mask.metrics();
    const std::span<const std::uint8_t> pixels = mask.coverage();
    require(metrics.width_px > 0 && metrics.height_px > 0, "Preview contains real glyph ink");
    std::ofstream output(path, std::ios::binary);
    output << "P5\n" << metrics.width_px << ' ' << metrics.height_px << "\n255\n";
    std::array<char, 4096> row{};
    for (std::uint32_t y = 0; y < metrics.height_px; ++y) {
        const std::size_t start = static_cast<std::size_t>(y) * metrics.stride_bytes;
        for (std::uint32_t x = 0; x < metrics.width_px; ++x) {
            row[x] = static_cast<char>(255U - pixels[start + x]);
        }
        output.write(row.data(), static_cast<std::streamsize>(metrics.width_px));
    }
    require(static_cast<bool>(output), "Write complete native mask preview");
}
void check_wrapping(Fixture& fixture, const std::filesystem::path& previews) {
    gui_forms::TextMaskRequest request{};
    request.utf8 = "Should you fail to name my code within ten attempts, I shall fold the Moon into a small paper swan, while a full orchestra plays.";
    request.size = 14;
    request.wrap_width = 180;
    gui_forms::TextMaskLease baseline{};
    gui_forms::TextMaskResult result = fixture.prepare(request, baseline);
    require(result.status == TextMaskStatus::success && baseline.lines().size() > 1, "Actual game dialogue wraps through the shared service");
    check_mask(baseline);
    for (const double scale : {.5, 1.25, 1.5, 2.0, 3.0, 4.0}) {
        request.device_scale = scale;
        gui_forms::TextMaskLease candidate{};
        result = fixture.prepare(request, candidate);
        require(result.status == TextMaskStatus::success, "Native grayscale mask renders at supported DPI");
        check_mask(candidate);
        same_lines(baseline, candidate);
        if (!previews.empty() && scale == 1.5) { write_preview(candidate, previews / "dialogue-gray-150.pgm"); }
    }
    request.device_scale = 1;
    gui_forms::TextMaskLease cached{};
    result = (*fixture.requests).request(request, cached);
    require(result.status == TextMaskStatus::success && cached.coverage().data() == baseline.coverage().data(),
        "Warm cache returns the same immutable coverage owner synchronously");
    const std::vector<std::uint8_t> before(baseline.coverage().begin(), baseline.coverage().end());
    (*fixture.session).clear_cache();
    require(std::equal(before.begin(), before.end(), baseline.coverage().begin(), baseline.coverage().end()),
        "A retained frame's real mask survives cache eviction");
}
void check_mono(Fixture& fixture, const std::filesystem::path& previews) {
    gui_forms::TextMaskRequest request{.utf8 = "EGGY", .primary_face = 1, .size = 14,
        .raster = gui_forms::TextMaskRaster::true_mono};
    gui_forms::TextMaskLease baseline{};
    gui_forms::TextMaskResult result = fixture.prepare(request, baseline);
    require(result.status == TextMaskStatus::success, "Real monochrome glyphs render");
    for (const double scale : {1.0, 2.0, 3.0, 4.0}) {
        request.device_scale = scale;
        gui_forms::TextMaskLease mask{};
        result = fixture.prepare(request, mask);
        require(result.status == TextMaskStatus::success, "Integer mono DPI renders");
        check_mask(mask);
        same_lines(baseline, mask);
        std::size_t ink = 0;
        for (const std::uint8_t coverage : mask.coverage()) {
            require(coverage == 0 || coverage == 255, "True mono output contains only binary coverage");
            if (coverage == 255) { ++ink; }
        }
        require(ink > 0, "Mono success contains visible native glyphs");
        if (!previews.empty() && scale == 3) { write_preview(mask, previews / "eggy-mono-300.pgm"); }
    }
    request.device_scale = 1.25;
    const std::uint8_t* const kept = baseline.coverage().data();
    result = fixture.prepare(request, baseline);
    require(result.status != TextMaskStatus::success && baseline.coverage().data() == kept,
        "Unsupported fractional mono DPI preserves the previous native mask");
}
void check_lines_and_failures(Fixture& fixture) {
    gui_forms::TextMaskLease mask{};
    gui_forms::TextMaskResult result = fixture.prepare({.utf8 = "First\r\n\r\nLast\n"}, mask);
    require(result.status == TextMaskStatus::success && mask.lines().size() == 4, "CRLF and trailing empty lines survive the consumer");
    check_mask(mask);
    result = fixture.prepare({}, mask);
    require(result.status == TextMaskStatus::success && mask.has_value() && mask.coverage().empty() &&
        mask.metrics().logical_height > 0, "Empty text is a valid zero-ink result with logical height");
    result = fixture.prepare({.utf8 = "e\xCC\x81" "e\xCC\x81", .primary_face = 1, .wrap_width = 1}, mask);
    require(result.status == TextMaskStatus::success && mask.metrics().horizontal_overflow, "Overwide combining clusters remain intact");
    check_mask(mask);
    for (const gui_forms::TextMaskLine& line : mask.lines()) {
        require(line.source_begin % 3 == 0 && line.text_end % 3 == 0, "No line breaks inside the base-plus-accent sequence");
    }
    result = fixture.prepare({.utf8 = "kept real mask"}, mask);
    require(result.status == TextMaskStatus::success, "Prepare failure-preservation baseline");
    const std::uint8_t* const kept = mask.coverage().data();
    const std::string excessive_width(1000, 'W');
    result = fixture.prepare({.utf8 = excessive_width, .size = 128, .device_scale = 4}, mask);
    require(result.status == TextMaskStatus::limit_exceeded &&
        result.limit == gui_forms::TextMaskLimit::mask_dimension && mask.coverage().data() == kept,
        "Late native dimension refusal preserves prior frame coverage");
}
}
int main(const int argc, char** const argv) {
    try {
        require(argc == 2 || argc == 3, "Pass approved font directory and optional preview directory");
        const std::filesystem::path fonts(argv[1]);
        std::filesystem::path previews{};
        if (argc == 3) { previews = argv[2]; std::filesystem::create_directories(previews); }
        gui_forms::TextMaskLease surviving{};
        std::vector<std::uint8_t> before{};
        {
            Fixture fixture(fonts);
            check_wrapping(fixture, previews);
            check_mono(fixture, previews);
            check_lines_and_failures(fixture);
            const gui_forms::TextMaskResult result = fixture.prepare({.utf8 = "Survives view closure"}, surviving);
            require(result.status == TextMaskStatus::success, "Prepare retained frame owner");
            before.assign(surviving.coverage().begin(), surviving.coverage().end());
        }
        require(surviving.has_value() && std::equal(before.begin(), before.end(), surviving.coverage().begin(), surviving.coverage().end()),
            "Native mask remains readable after queue, session and service destruction");
        std::cout << "Native consumer wrapping, DPI, mono, source, limits and retained ownership passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
