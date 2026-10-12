#include "pixel_surface.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace render {

void PixelSurface::invalidate_all() {
    owed_ = Rect{};
    shown_.clear();
    shown_texts_.clear();
    surface_.invalidate_all();
}

std::optional<Surface::Frame> PixelSurface::begin(const r2d::Canvas& low, double scale, const std::vector<Text>& texts) {
    if (low.px.size() < static_cast<std::size_t>(std::max(low.w, 0)) * static_cast<std::size_t>(std::max(low.h, 0)) * 4) {
        return std::nullopt;
    }
    return begin(reinterpret_cast<const std::uint32_t*>(low.px.data()), low.w, low.h, scale, scale, texts);
}

std::optional<Surface::Frame> PixelSurface::begin(const std::uint32_t* low, int w, int h, double scale_x, double scale_y,
                                                  const std::vector<Text>& texts) {
    const int width = surface_.width();
    const int height = surface_.height();
    if (low == nullptr || w <= 0 || h <= 0 || width <= 0 || height <= 0 || scale_x <= 0 || scale_y <= 0) {
        return std::nullopt;
    }
    // What changed: the small frame's changed rows and columns, enlarged to the window,
    // and every text that is new, gone or different.
    Rect damage;
    const std::size_t words = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    const std::size_t row_bytes = static_cast<std::size_t>(w) * 4;
    if (shown_w_ != w || shown_h_ != h || shown_scale_x_ != scale_x || shown_scale_y_ != scale_y || shown_.size() != words) {
        damage = Rect{0, 0, width, height};
        shown_.assign(low, low + words);
        shown_w_ = w;
        shown_h_ = h;
        shown_scale_x_ = scale_x;
        shown_scale_y_ = scale_y;
        columns_.clear();
    } else {
        int first_row = h;
        int last_row = -1;
        int first_column = w;
        int last_column = -1;
        for (int y = 0; y < h; ++y) {
            const std::uint32_t* a = low + static_cast<std::size_t>(y) * static_cast<std::size_t>(w);
            std::uint32_t* b = shown_.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w);
            if (std::memcmp(a, b, row_bytes) == 0) {
                continue;
            }
            first_row = std::min(first_row, y);
            last_row = y;
            // the changed columns: from each end inward to the first difference
            int x0 = 0;
            while (x0 < w && a[x0] == b[x0]) ++x0;
            int x1 = w - 1;
            while (x1 > x0 && a[x1] == b[x1]) --x1;
            first_column = std::min(first_column, x0);
            last_column = std::max(last_column, x1);
            std::memcpy(b, a, row_bytes);
        }
        if (last_row >= 0) {
            damage = Rect{static_cast<int>(std::floor(first_column * scale_x)) - 1,
                          static_cast<int>(std::floor(first_row * scale_y)) - 1,
                          static_cast<int>(std::ceil((last_column + 1) * scale_x)) + 1,
                          static_cast<int>(std::ceil((last_row + 1) * scale_y)) + 1};
        }
    }
    bool same_texts = texts.size() == shown_texts_.size();
    for (std::size_t i = 0; same_texts && i < texts.size(); ++i) {
        same_texts = texts[i].key == shown_texts_[i].key && texts[i].bounds.x0 == shown_texts_[i].bounds.x0 &&
                     texts[i].bounds.y0 == shown_texts_[i].bounds.y0 && texts[i].bounds.x1 == shown_texts_[i].bounds.x1 &&
                     texts[i].bounds.y1 == shown_texts_[i].bounds.y1;
    }
    if (!same_texts) {
        for (const Text& text : shown_texts_) damage = damage.united(text.bounds);
        for (const Text& text : texts) damage = damage.united(text.bounds);
        shown_texts_ = texts;
    }
    // Changes from frames given up are still owed.
    damage = damage.united(owed_).intersected(Rect{0, 0, width, height});
    owed_ = damage;
    if (damage.empty()) {
        // The picture shown is still right; no buffer is taken.
        return std::nullopt;
    }
    std::optional<Surface::Frame> frame = surface_.begin(damage);
    if (!frame) {
        return std::nullopt;
    }
    const Target& target = (*frame).target;
    const Rect repair = (*frame).repair;
    if (static_cast<int>(columns_.size()) != width) {
        columns_.resize(static_cast<std::size_t>(width));
        for (int x = 0; x < width; ++x) {
            columns_[static_cast<std::size_t>(x)] = std::min(w - 1, static_cast<int>(x / scale_x));
        }
    }
    // Enlarge the repaired window pixels: each game row once into the window's byte
    // order, then copied to the window rows it covers.
    row_.resize(static_cast<std::size_t>(w));
    const bool swap = target.order == Order::rgba;
    int converted = -1;
    for (int y = repair.y0; y < repair.y1; ++y) {
        const int source_row = std::min(h - 1, static_cast<int>(y / scale_y));
        if (source_row != converted) {
            const std::uint32_t* source = low + static_cast<std::size_t>(source_row) * static_cast<std::size_t>(w);
            for (int x = 0; x < w; ++x) {
                const std::uint32_t p = source[x];
                row_[static_cast<std::size_t>(x)] = swap ? (p & 0xFF00FF00U) | ((p >> 16) & 0xFFU) | ((p & 0xFFU) << 16) : p;
            }
            converted = source_row;
        }
        std::uint32_t* out = target.row(y);
        for (int x = repair.x0; x < repair.x1; ++x) {
            out[x] = row_[static_cast<std::size_t>(columns_[static_cast<std::size_t>(x)])];
        }
    }
    return frame;
}

}  // namespace render
