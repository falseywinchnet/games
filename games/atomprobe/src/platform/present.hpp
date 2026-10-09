#pragma once
// Native fast presentation (macOS): the low-resolution game frame becomes the
// contents of a Core Animation layer that the GPU magnifies with nearest-
// neighbour filtering, and each crisp text run is a small cached layer.
// This avoids the framework's per-frame CPU rasterisation of the whole window.
#include "render.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ap {

struct TextSprite {
    std::string key;     // identity of the rendered image (text, font, colour, scale)
    const Mask* mask;    // glyph coverage at 1 font pixel
    Col color;
    int scale;           // physical pixels per font pixel
    bool shadow;
    double x, y;         // top-left in points
};

class NativePresenter {
public:
    ~NativePresenter();
    bool attach(void* host_window_hint = nullptr);  // finds the app window's content view
    bool attached() const { return impl_ != nullptr; }
    // `x,y,w,h`: the control's rectangle in window points (top-left origin)
    void frame(const Canvas& low, double x, double y, double w, double h, double image_w, double image_h);
    void set_visible(bool v);
    void texts(const std::vector<TextSprite>& items);
    void detach();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace ap

namespace ap {
// The system pointer: hidden while she plays with the game's own pointer, and
// moved to where she dropped it. Coordinates are points in the window's
// content view, top-left origin.
void cursor_set_hidden(bool hidden);
void cursor_warp(double x, double y);
}  // namespace ap
