#include "r3d.hpp"

#include <algorithm>

namespace render::r3d {
namespace {

int word(std::span<const std::uint8_t> file, std::size_t at) {
    return static_cast<int>(file[at] | (file[at + 1] << 8) | (file[at + 2] << 16) | (file[at + 3] << 24));
}

// Two 8-bit lanes (0x00ff00ff) blended from p to q by w / 256.
std::uint32_t mix(std::uint32_t p, std::uint32_t q, std::uint32_t w) {
    const std::uint32_t rb = ((p & 0x00ff00ffU) * (256 - w) + (q & 0x00ff00ffU) * w) >> 8;
    const std::uint32_t ga = (((p >> 8) & 0x00ff00ffU) * (256 - w) + ((q >> 8) & 0x00ff00ffU) * w) >> 8;
    return (rb & 0x00ff00ffU) | ((ga & 0x00ff00ffU) << 8);
}

// Two 8-bit lanes times k / 255, rounded: t + t / 256, then / 256.
std::uint32_t scale_lanes(std::uint32_t lanes, std::uint32_t k) {
    const std::uint32_t t = lanes * k + 0x00800080U;
    return ((t + ((t >> 8) & 0x00ff00ffU)) >> 8) & 0x00ff00ffU;
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

std::uint32_t Panorama::nearest(float u, float v) const {
    float x = u * static_cast<float>(width_);
    x -= std::floor(x / static_cast<float>(width_)) * static_cast<float>(width_);
    const int sx = std::clamp(static_cast<int>(x), 0, width_ - 1);
    const int sy = std::clamp(static_cast<int>(v * static_cast<float>(height_)), 0, height_ - 1);
    return texels_[static_cast<std::size_t>(sy) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(sx)];
}

std::uint32_t Panorama::bilinear(float u, float v) const {
    // Longitude wraps; latitude clamps. Positions in 8-bit fixed point, two channels per
    // multiply; a power-of-two width wraps with a mask.
    int sx = 0;
    int ex = 0;
    std::uint32_t wx = 0;
    if (wrap_mask_ != 0) {
        const int fx = static_cast<int>(u * static_cast<float>(width_) * 256.0F) - 128;
        sx = (fx >> 8) & wrap_mask_;
        ex = (sx + 1) & wrap_mask_;
        wx = static_cast<std::uint32_t>(fx & 255);
    } else {
        float x = u * static_cast<float>(width_) - .5F;
        x -= std::floor(x / static_cast<float>(width_)) * static_cast<float>(width_);
        sx = std::min(static_cast<int>(x), width_ - 1);
        ex = sx + 1 == width_ ? 0 : sx + 1;
        wx = static_cast<std::uint32_t>((x - static_cast<float>(sx)) * 256.0F);
    }
    const int fy = std::clamp(static_cast<int>(v * static_cast<float>(height_) * 256.0F) - 128, 0, (height_ - 1) * 256);
    const int sy = fy >> 8;
    const int ey = std::min(sy + 1, height_ - 1);
    const std::uint32_t wy = static_cast<std::uint32_t>(fy & 255);
    const std::uint32_t* top = texels_.data() + static_cast<std::size_t>(sy) * static_cast<std::size_t>(width_);
    const std::uint32_t* bottom = texels_.data() + static_cast<std::size_t>(ey) * static_cast<std::size_t>(width_);
    return mix(mix(top[sx], top[ex], wx), mix(bottom[sx], bottom[ex], wx), wy);
}

template <bool Nearest>
Mirror<Nearest> Mirror<Nearest>::make(const Panorama& panorama, Color tint, float share) {
    Mirror<Nearest> m;
    m.panorama = &panorama;
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

bool Over::shade(std::uint32_t* pixel, const float*) const {
    // Premultiplied "over", two channels per multiply; t + t / 256 then / 256 divides
    // by 255 with rounding.
    const std::uint32_t d = *pixel;
    const std::uint32_t kept = scale_lanes(d & 0x00ff00ffU, keep) | (scale_lanes((d >> 8) & 0x00ff00ffU, keep) << 8);
    // Lanes cannot overflow: source + destination * (1 - alpha) <= 255 for valid
    // premultiplied pixels.
    *pixel = premultiplied + kept;
    return claims;
}

}  // namespace render::r3d
