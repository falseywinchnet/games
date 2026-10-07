#include "image.hpp"

#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>

#include <algorithm>
#include <cmath>

namespace zc {

bool load_png(const std::string& path, Canvas& out) {
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
        CGImageSourceRef src = CGImageSourceCreateWithURL((__bridge CFURLRef)url, nullptr);
        if (!src) return false;
        CGImageRef img = CGImageSourceCreateImageAtIndex(src, 0, nullptr);
        CFRelease(src);
        if (!img) return false;
        const int w = static_cast<int>(CGImageGetWidth(img)), h = static_cast<int>(CGImageGetHeight(img));
        out.resize(w, h);
        CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        CGContextRef ctx = CGBitmapContextCreate(out.px.data(), static_cast<size_t>(w), static_cast<size_t>(h), 8, static_cast<size_t>(w) * 4, cs,
                                                 kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
        CGColorSpaceRelease(cs);
        if (!ctx) { CGImageRelease(img); return false; }
        CGContextClearRect(ctx, CGRectMake(0, 0, w, h));
        CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), img);
        CGContextRelease(ctx);
        CGImageRelease(img);
        return true;
    }
}

void reduce(const Canvas& src, int w, int h, Canvas& out) {
    w = std::max(1, w);
    h = std::max(1, h);
    out.resize(w, h);
    const double sx = static_cast<double>(src.w) / w, sy = static_cast<double>(src.h) / h;
    for (int y = 0; y < h; ++y) {
        const double y0 = y * sy, y1 = (y + 1) * sy;
        for (int x = 0; x < w; ++x) {
            const double x0 = x * sx, x1 = (x + 1) * sx;
            double acc[4] = {0, 0, 0, 0}, wt = 0;
            for (int yy = static_cast<int>(y0); yy < std::min(src.h, static_cast<int>(std::ceil(y1))); ++yy) {
                const double fy = std::min(y1, yy + 1.0) - std::max(y0, static_cast<double>(yy));
                for (int xx = static_cast<int>(x0); xx < std::min(src.w, static_cast<int>(std::ceil(x1))); ++xx) {
                    const double f = fy * (std::min(x1, xx + 1.0) - std::max(x0, static_cast<double>(xx)));
                    const std::uint8_t* p = &src.px[(static_cast<size_t>(yy) * src.w + xx) * 4];
                    for (int c = 0; c < 4; ++c) acc[c] += p[c] * f;
                    wt += f;
                }
            }
            std::uint8_t* o = &out.px[(static_cast<size_t>(y) * w + x) * 4];
            for (int c = 0; c < 4; ++c) o[c] = static_cast<std::uint8_t>(std::clamp(acc[c] / std::max(wt, 1e-9) + .5, 0.0, 255.0));
        }
    }
}

}  // namespace zc
