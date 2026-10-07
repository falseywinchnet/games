#include "present.hpp"

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace fp {

struct NativePresenter::Impl {
    NSView* view = nil;
    CALayer* root = nil;
    CALayer* image = nil;
    std::vector<CALayer*> text_layers;
    std::vector<std::string> text_keys;
    std::unordered_map<std::string, CGImageRef> cache;
    CGColorSpaceRef space = nullptr;
    CGColorSpaceRef frame_space = nullptr;  // the window's own colour space: no conversion work
    double backing = 2;
};

namespace {
CGImageRef make_image(const void* bgra, int w, int h, CGColorSpaceRef space, bool opaque = false) {
    CFDataRef data = CFDataCreate(nullptr, static_cast<const UInt8*>(bgra), static_cast<CFIndex>(w) * h * 4);
    CGDataProviderRef prov = CGDataProviderCreateWithCFData(data);
    CGImageRef img = CGImageCreate(static_cast<size_t>(w), static_cast<size_t>(h), 8, 32, static_cast<size_t>(w) * 4, space,
                                   kCGBitmapByteOrder32Little | (opaque ? kCGImageAlphaNoneSkipFirst : kCGImageAlphaPremultipliedFirst), prov, nullptr, false,
                                   kCGRenderingIntentDefault);
    CGDataProviderRelease(prov);
    CFRelease(data);
    return img;
}
}  // namespace

NativePresenter::~NativePresenter() { detach(); }

bool NativePresenter::attach(void*) {
    if (impl_) return true;
    NSWindow* win = [NSApp mainWindow];
    if (!win) win = [NSApp keyWindow];
    if (!win)
        for (NSWindow* w in [NSApp windows])
            if (w.contentView && w.isVisible) { win = w; break; }
    if (!win || !win.contentView) return false;
    NSView* v = win.contentView;
    if (!v.layer) return false;
    impl_ = new Impl();
    impl_->view = [v retain];
    impl_->backing = win.backingScaleFactor;
    impl_->space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    impl_->frame_space = win.colorSpace.CGColorSpace ? CGColorSpaceRetain(win.colorSpace.CGColorSpace) : CGColorSpaceRetain(impl_->space);
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    impl_->root = [[CALayer alloc] init];
    impl_->root.frame = v.bounds;
    impl_->root.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
    impl_->root.zPosition = 1000;
    impl_->root.masksToBounds = YES;
    impl_->root.geometryFlipped = !v.isFlipped;  // top-left origin for our sublayers
    impl_->image = [[CALayer alloc] init];
    impl_->image.magnificationFilter = kCAFilterNearest;
    impl_->image.minificationFilter = kCAFilterNearest;
    impl_->image.contentsGravity = kCAGravityResize;
    impl_->image.backgroundColor = CGColorGetConstantColor(kCGColorBlack);
    [impl_->root addSublayer:impl_->image];
    [v.layer addSublayer:impl_->root];
    [CATransaction commit];
    if (std::getenv("FP_PRINT_WID")) std::fprintf(stderr, "FP_WINDOW_ID %ld\n", static_cast<long>(win.windowNumber));
    return true;
}

void NativePresenter::set_visible(bool v) {
    if (!impl_ || impl_->root.hidden == !v) return;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    impl_->root.hidden = !v;
    [CATransaction commit];
}

void NativePresenter::frame(const Canvas& low, double x, double y, double w, double h, double image_w, double image_h) {
    if (!impl_) return;
    CGImageRef img = make_image(low.px.data(), low.w, low.h, impl_->frame_space, true);
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    // place our layer exactly over the control (it may be one tab of a larger window)
    const CGFloat vh = impl_->view.bounds.size.height;
    impl_->root.frame = impl_->view.isFlipped ? CGRectMake(x, y, w, h) : CGRectMake(x, vh - y - h, w, h);
    impl_->image.frame = CGRectMake(0, 0, image_w, image_h);  // game pixels, magnified on the GPU
    impl_->image.contents = (__bridge id)img;
    [CATransaction commit];
    CGImageRelease(img);
}

void NativePresenter::texts(const std::vector<TextSprite>& items) {
    if (!impl_) return;
    if (impl_->cache.size() > 400) {
        for (auto& [k, v] : impl_->cache) CGImageRelease(v);
        impl_->cache.clear();
        impl_->text_keys.assign(impl_->text_keys.size(), std::string());
    }
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    const double bs = impl_->backing;
    for (size_t i = 0; i < items.size(); ++i) {
        const TextSprite& t = items[i];
        if (i >= impl_->text_layers.size()) {
            CALayer* l = [[CALayer alloc] init];
            l.magnificationFilter = kCAFilterNearest;
            l.minificationFilter = kCAFilterNearest;
            l.contentsScale = bs;
            l.anchorPoint = CGPointMake(0, 0);
            [impl_->root addSublayer:l];
            impl_->text_layers.push_back(l);
            impl_->text_keys.emplace_back();
        }
        CALayer* l = impl_->text_layers[i];
        const int sc = std::max(1, t.scale), sh = t.shadow ? sc : 0;
        const int w = t.mask->w * sc + sh, h = t.mask->h * sc + sh;
        auto it = impl_->cache.find(t.key);
        CGImageRef img;
        if (it == impl_->cache.end()) {
            std::vector<std::uint32_t> px(static_cast<size_t>(w) * h, 0);
            auto put = [&](int x, int y, Col c, float cov) {
                std::uint32_t& d = px[static_cast<size_t>(y) * w + x];
                const float a = c.a * cov, k = 1 - a;
                const float db = static_cast<float>(d & 255), dg = static_cast<float>((d >> 8) & 255), dr = static_cast<float>((d >> 16) & 255), da = static_cast<float>(d >> 24);
                const auto q = [](float v) { return static_cast<std::uint32_t>(std::min(255.f, v + .5f)); };
                d = q(c.b * a * 255 + db * k) | (q(c.g * a * 255 + dg * k) << 8) | (q(c.r * a * 255 + dr * k) << 16) | (q(a * 255 + da * k) << 24);
            };
            for (int pass = t.shadow ? 0 : 1; pass < 2; ++pass) {
                const Col c = pass == 0 ? Col{0, 0, 0, .7f * t.color.a} : t.color;
                const int off = pass == 0 ? sh : 0;
                for (int my = 0; my < t.mask->h * sc; ++my)
                    for (int mx = 0; mx < t.mask->w * sc; ++mx) {
                        const float cov = t.mask->a[static_cast<size_t>(my / sc) * t.mask->w + mx / sc] * (1.f / 255.f);
                        if (cov > 0) put(mx + off, my + off, c, cov);
                    }
            }
            img = make_image(px.data(), w, h, impl_->space);
            impl_->cache.emplace(t.key, img);
        } else {
            img = it->second;
        }
        if (impl_->text_keys[i] != t.key) {
            l.contents = (__bridge id)img;
            l.bounds = CGRectMake(0, 0, w / bs, h / bs);
            impl_->text_keys[i] = t.key;
        }
        l.position = CGPointMake(t.x, t.y);
        l.hidden = NO;
    }
    for (size_t i = items.size(); i < impl_->text_layers.size(); ++i) impl_->text_layers[i].hidden = YES;
    [CATransaction commit];
}

void NativePresenter::detach() {
    if (!impl_) return;
    [impl_->root removeFromSuperlayer];
    for (CALayer* l : impl_->text_layers) [l release];
    [impl_->image release];
    [impl_->root release];
    [impl_->view release];
    for (auto& [k, v] : impl_->cache) CGImageRelease(v);
    if (impl_->space) CGColorSpaceRelease(impl_->space);
    if (impl_->frame_space) CGColorSpaceRelease(impl_->frame_space);
    delete impl_;
    impl_ = nullptr;
}

void cursor_set_hidden(bool hidden) {
    static bool is_hidden = false;
    if (hidden == is_hidden) return;
    is_hidden = hidden;
    if (hidden) [NSCursor hide];
    else [NSCursor unhide];
}

void cursor_warp(double x, double y) {
    NSWindow* win = [NSApp keyWindow];
    if (!win) win = [NSApp mainWindow];
    if (!win || !win.contentView || NSScreen.screens.count == 0) return;
    NSView* v = win.contentView;
    const NSPoint p = v.isFlipped ? NSMakePoint(x, y) : NSMakePoint(x, v.bounds.size.height - y);
    const NSPoint sp = [win convertPointToScreen:[v convertPoint:p toView:nil]];
    // Quartz global coordinates run down from the top of the primary screen
    const CGFloat top = NSScreen.screens[0].frame.size.height;
    CGWarpMouseCursorPosition(CGPointMake(sp.x, top - sp.y));
    CGAssociateMouseAndMouseCursorPosition(true);
}

}  // namespace fp
