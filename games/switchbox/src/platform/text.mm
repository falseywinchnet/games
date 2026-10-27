#include "text.hpp"

#import <CoreText/CoreText.h>
#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <cmath>
#include <unordered_map>

namespace sbx {

namespace {
struct Key {
    std::string s;
    int font;
    int size10;
    int wrap;
    bool operator==(const Key& o) const { return s == o.s && font == o.font && size10 == o.size10 && wrap == o.wrap; }
};
struct KeyHash {
    size_t operator()(const Key& k) const {
        return std::hash<std::string>()(k.s) ^ (static_cast<size_t>(k.font) << 1) ^ (static_cast<size_t>(k.size10) << 7) ^
               (static_cast<size_t>(k.wrap) << 17);
    }
};
std::unordered_map<Key, Mask, KeyHash>& cache() {
    static std::unordered_map<Key, Mask, KeyHash> c;
    return c;
}

CTFontRef make_font(Font f, double size) {
    const char* names[] = {"ChalkboardSE-Regular", "ChalkboardSE-Bold", "Monaco", "Monaco", "ChalkboardSE-Bold", "AvenirNext-DemiBold"};
    CFStringRef name = CFStringCreateWithCString(nullptr, names[static_cast<int>(f)], kCFStringEncodingUTF8);
    CTFontRef font = CTFontCreateWithName(name, size, nullptr);
    CFRelease(name);
    if (f == Font::pixel_bold) {
        CTFontRef bold = CTFontCreateCopyWithSymbolicTraits(font, size, nullptr, kCTFontBoldTrait, kCTFontBoldTrait);
        if (bold) { CFRelease(font); font = bold; }
    }
    return font;
}
}  // namespace

void text_cache_trim() {
    if (cache().size() > 700) cache().clear();
}

const Mask& text_mask(const std::string& utf8, Font font, double size, double wrap_width) {
    Key key{utf8, static_cast<int>(font), static_cast<int>(size * 10), static_cast<int>(wrap_width)};
    auto it = cache().find(key);
    if (it != cache().end()) return it->second;
    Mask m;
    @autoreleasepool {
        CTFontRef ctfont = make_font(font, size);
        NSString* str = [NSString stringWithUTF8String:utf8.c_str()];
        if (!str) str = @"";
        CGColorRef white = CGColorCreateGenericGray(1, 1);
        NSMutableParagraphStyle* para = [[NSMutableParagraphStyle alloc] init];
        para.lineBreakMode = NSLineBreakByWordWrapping;
        para.alignment = NSTextAlignmentLeft;
        para.lineSpacing = size * .05;
        NSDictionary* attrs = @{(id)kCTFontAttributeName: (__bridge id)ctfont,
                                (id)kCTForegroundColorAttributeName: (__bridge id)white,
                                NSParagraphStyleAttributeName: para};
        NSAttributedString* as = [[NSAttributedString alloc] initWithString:str attributes:attrs];
        CTFramesetterRef fs = CTFramesetterCreateWithAttributedString((__bridge CFAttributedStringRef)as);
        const CGFloat maxw = wrap_width > 0 ? wrap_width : 100000;
        CGSize sz = CTFramesetterSuggestFrameSizeWithConstraints(fs, CFRangeMake(0, 0), nullptr, CGSizeMake(maxw, 100000), nullptr);
        m.w = std::max(1, static_cast<int>(std::ceil(sz.width)) + 2);
        m.h = std::max(1, static_cast<int>(std::ceil(sz.height)) + 2);
        m.a.assign(static_cast<size_t>(m.w) * m.h, 0);
        CGContextRef ctx = CGBitmapContextCreate(m.a.data(), static_cast<size_t>(m.w), static_cast<size_t>(m.h), 8,
                                                 static_cast<size_t>(m.w), nullptr, kCGImageAlphaOnly);
        if (ctx) {
            const bool aa = !(font == Font::pixel || font == Font::pixel_bold);
            CGContextSetShouldAntialias(ctx, aa);
            CGContextSetAllowsAntialiasing(ctx, aa);
            CGContextSetShouldSmoothFonts(ctx, false);
            CGMutablePathRef path = CGPathCreateMutable();
            CGPathAddRect(path, nullptr, CGRectMake(1, 1, m.w - 1, m.h - 1));
            CTFrameRef frame = CTFramesetterCreateFrame(fs, CFRangeMake(0, 0), path, nullptr);
            CTFrameDraw(frame, ctx);
            CFRelease(frame);
            CGPathRelease(path);
            CGContextRelease(ctx);
        }
        CFRelease(fs);
        [as release];
        [para release];
        CGColorRelease(white);
        CFRelease(ctfont);
    }
    return cache().emplace(std::move(key), std::move(m)).first->second;
}

int draw_text(Canvas& c, const std::string& s, Font f, double size, int x, int y, Col col, int scale, Col shadow, double wrap) {
    const Mask& m = text_mask(s, f, size, wrap);
    if (shadow.a > 0) c.draw_mask(m, x + scale, y + scale, shadow, scale);
    c.draw_mask(m, x, y, col, scale);
    return m.w * scale;
}

}  // namespace sbx
