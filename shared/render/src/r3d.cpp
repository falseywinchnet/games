#include "r3d.hpp"

#include <algorithm>

namespace render::r3d {
namespace {

int word(std::span<const std::uint8_t> file, std::size_t at) {
    return static_cast<int>(file[at] | (file[at + 1] << 8) | (file[at + 2] << 16) | (file[at + 3] << 24));
}

}  // namespace

void Buffers::resize(int width, int height, bool ids) {
    width_ = std::max(0, width);
    height_ = std::max(0, height);
    const std::size_t count = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
    depth_.assign(count, detail::far_depth);
    if (ids) {
        ids_.assign(count, -1);
    } else {
        ids_.clear();
    }
}

void Buffers::clear(Rect area) {
    area = area.intersected(Rect{0, 0, width_, height_});
    if (area.empty()) {
        return;
    }
    const std::size_t span = static_cast<std::size_t>(area.x1 - area.x0);
    for (int y = area.y0; y < area.y1; ++y) {
        const std::size_t at = static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(area.x0);
        std::fill_n(depth_.begin() + static_cast<std::ptrdiff_t>(at), span, detail::far_depth);
        if (!ids_.empty()) {
            std::fill_n(ids_.begin() + static_cast<std::ptrdiff_t>(at), span, -1);
        }
    }
}

bool Panorama::load_gpix(std::span<const std::uint8_t> file, Order order, float gain, float lift) {
    texels_.clear();
    width_ = 0;
    height_ = 0;
    if (file.size() < 16 || file[0] != 'G' || file[1] != 'P' || file[2] != 'I' || file[3] != 'X' || file[4] != 1) {
        return false;
    }
    const int width = word(file, 8);
    const int height = word(file, 12);
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
        file.size() != 16 + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4) {
        return false;
    }
    texels_.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (std::size_t index = 0; index < texels_.size(); ++index) {
        const std::uint8_t* p = file.data() + 16 + index * 4;
        texels_[index] = pack(order, p[0] * gain + lift, p[1] * gain + lift, p[2] * gain + lift, 255);
    }
    width_ = width;
    height_ = height;
    wrap_mask_ = (width & (width - 1)) == 0 ? width - 1 : 0;
    order_ = order;
    return true;
}

template <bool Nearest>
Mirror<Nearest> Mirror<Nearest>::make(const Panorama& panorama, Color tint, float share) {
    Mirror<Nearest> m;
    m.panorama = panorama.view();
    m.mirror = static_cast<std::uint32_t>(std::clamp(share, 0.0F, 1.0F) * 256.0F + .5F);
    // Rounded down, so the reflection's part and the tint's never pass 255 together.
    const float keep = static_cast<float>(256 - m.mirror) / 256.0F;
    const std::uint32_t red = static_cast<std::uint32_t>(std::clamp(tint.r, 0.0F, 255.0F) * keep);
    const std::uint32_t blue = static_cast<std::uint32_t>(std::clamp(tint.b, 0.0F, 255.0F) * keep);
    const bool bgra = panorama.order() == Order::bgra;
    m.tint_rb = (bgra ? blue : red) | ((bgra ? red : blue) << 16);
    m.tint_g = static_cast<std::uint32_t>(std::clamp(tint.g, 0.0F, 255.0F) * keep);
    return m;
}
template struct Mirror<false>;
template struct Mirror<true>;

Over Over::make(Order order, Color color) {
    Over o;
    o.premultiplied = pack(order, color);
    o.keep = 255 - (o.premultiplied >> 24);
    o.claims = color.a > 127.5F;
    return o;
}

}  // namespace render::r3d
