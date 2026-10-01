#include "puzzle_render.hpp"
#import <AppKit/AppKit.h>
namespace games {
void PuzzleRaster::load_environment(const std::string& file) {
    NSImage* image =
        [[NSImage alloc] initWithContentsOfFile:[NSString stringWithUTF8String:file.c_str()]];
    if (!image)
        return;
    CGImageRef cg = [image CGImageForProposedRect:nil context:nil hints:nil];
    env_width_ = 1024;
    env_height_ = 512;
    environment_.resize(env_width_ * env_height_ * 4);
    CGColorSpaceRef color = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(
        environment_.data(), env_width_, env_height_, 8, env_width_ * 4, color,
        static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast) | kCGBitmapByteOrder32Big);
    if (context) {
        CGContextSetInterpolationQuality(context, kCGInterpolationHigh);
        CGContextDrawImage(context, CGRectMake(0, 0, env_width_, env_height_), cg);
        CGContextRelease(context);
    } else
        environment_.clear();
    CGColorSpaceRelease(color);
    [image release];
}
} // namespace games
