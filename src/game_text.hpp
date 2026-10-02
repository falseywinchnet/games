#pragma once
#include "text_requests.hpp"
#include <filesystem>

namespace games {
struct TextImage final {
    // Logical advance and line height, rounded outward for game layout.
    int w{};
    int h{};
    gui_forms::TextMaskLease mask{};
};

// One view owns this service, bank and session on its UI executor. A render
// attempt calls begin(), requests every needed mask, then checks ready().
// A pending attempt keeps its model snapshot; it must not publish its pixels.
class GameText final {
public:
    GameText(const std::filesystem::path& fonts, const std::string& dialogue_family);
    void begin();
    void cancel();
    [[nodiscard]] TextImage get(const std::string_view source, const bool bold,
        const double size, const double wrap, const double scale,
        const bool mono = false);
    [[nodiscard]] bool ready() const { return ready_; }
private:
    // Destruction is reverse declaration order: broker, session, bank, service.
    gui_forms::TextMaskService service_{};
    gui_forms::EncodedFontLease fonts_{};
    std::unique_ptr<gui_forms::TextMaskSession> session_{};
    std::unique_ptr<TextRequests> requests_{};
    bool ready_{false};
};

// Blend native coverage into a complete, opaque BGRA game surface. The span
// owns no pixels and must contain height rows of stride pixels. Signed ink
// bearings are relative to the supplied logical top-left, in device pixels.
void blit_game_text(std::span<std::uint32_t> destination, const int width,
    const int height, const std::size_t stride, const TextImage& text,
    const int x, const int y, const double red, const double green,
    const double blue, const double alpha, const double magnification = 1);
}
