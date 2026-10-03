#pragma once
#include "game_text.hpp"
#include "runtime_paths.hpp"
#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <stdexcept>
#include <thread>
#include <tuple>

namespace games {
// Compatibility for delivered renderers whose layout API requires an immediate
// mask. The shared service does shaping on its worker; only a first cache miss
// waits here, with a bounded deadline. Each namespace owns its cache on the UI
// executor. Completed masks keep stable references until end-of-frame trim().
template <class Mask> class PortableMaskCache final {
  public:
    const Mask& get(const std::string& text, int font, double size, double wrap) {
        const Key key{text, font, size, wrap};
        typename std::map<Key, Mask>::const_iterator cached = masks_.find(key);
        if (cached != masks_.end()) {
            return (*cached).second;
        }
        if (!text_) {
            text_ = std::make_unique<GameText>(std::filesystem::path(asset_directory()) / "fonts",
                                               "LibreBaskerville");
        }
        TextImage image;
        const std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(2);
        do {
            (*text_).begin();
            image = (*text_).get(text, font == 1 || font == 3 || font == 4, size, wrap, 1,
                                 font == 2 || font == 3);
            if (image.mask.has_value()) {
                break;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                throw std::runtime_error("Timed out preparing game text");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (true);
        const gui_forms::TextMaskMetrics metrics = image.mask.metrics();
        Mask mask;
        mask.w = std::max(1, image.w + 2);
        mask.h = std::max(1, image.h + 2);
        mask.a.assign(static_cast<std::size_t>(mask.w) * mask.h, 0);
        const std::span<const std::uint8_t> coverage = image.mask.coverage();
        for (std::uint32_t y = 0; y < metrics.height_px; ++y) {
            const int dy = static_cast<int>(y) + metrics.ink_top_px + 1;
            if (dy < 0 || dy >= mask.h) {
                continue;
            }
            for (std::uint32_t x = 0; x < metrics.width_px; ++x) {
                const int dx = static_cast<int>(x) + metrics.ink_left_px + 1;
                if (dx >= 0 && dx < mask.w) {
                    mask.a[static_cast<std::size_t>(dy) * mask.w + dx] =
                        coverage[static_cast<std::size_t>(y) * metrics.stride_bytes + x];
                }
            }
        }
        return (*masks_.emplace(key, std::move(mask)).first).second;
    }
    void trim() {
        if (masks_.size() > 700) {
            masks_.clear();
        }
    }

  private:
    using Key = std::tuple<std::string, int, double, double>;
    std::unique_ptr<GameText> text_;
    std::map<Key, Mask> masks_;
};
} // namespace games
