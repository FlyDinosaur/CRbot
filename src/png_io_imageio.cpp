#include "png_io.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <string>
#include <vector>

// macOS 这边用系统自带的 ImageIO 解 PNG：
// 不用带 libpng/stb，中文路径也不用特殊处理（CoreFoundation 收 UTF-8）。

namespace {

constexpr size_t kMaxSide = 8192;

bool decode_source(CGImageSourceRef src, Image& out, std::string* err) {
    auto fail = [&](const char* why) {
        if (err) *err = why;
        return false;
    };
    if (!src) return fail("cannot open png");

    CGImageRef img = CGImageSourceCreateImageAtIndex(src, 0, nullptr);
    if (!img) return fail("cannot decode png");

    const size_t w = CGImageGetWidth(img);
    const size_t h = CGImageGetHeight(img);
    if (w == 0 || h == 0 || w > kMaxSide || h > kMaxSide) {
        CGImageRelease(img);
        return fail("bogus png size");
    }

    std::vector<uint8_t> rgba(w * h * 4, 0);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    const CGBitmapInfo info =
        static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast) |
        static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Big);
    CGContextRef ctx = CGBitmapContextCreate(rgba.data(), w, h, 8, w * 4, cs, info);
    CGColorSpaceRelease(cs);
    if (!ctx) {
        CGImageRelease(img);
        return fail("cannot create bitmap context");
    }

    CGContextSetBlendMode(ctx, kCGBlendModeCopy);
    CGContextDrawImage(ctx, CGRectMake(0, 0, static_cast<CGFloat>(w), static_cast<CGFloat>(h)),
                       img);
    CGContextRelease(ctx);
    CGImageRelease(img);

    out = Image(static_cast<int>(w), static_cast<int>(h), std::move(rgba));
    return !out.empty();
}

}  // namespace

bool load_png(const std::filesystem::path& path, Image& out, std::string* err) {
    const std::string p = path.string();
    if (p.empty()) {
        if (err) *err = "empty path";
        return false;
    }

    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault, reinterpret_cast<const UInt8*>(p.data()),
        static_cast<CFIndex>(p.size()), false);
    if (!url) {
        if (err) *err = "cannot create file url";
        return false;
    }

    CGImageSourceRef src = CGImageSourceCreateWithURL(url, nullptr);
    CFRelease(url);

    const bool ok = decode_source(src, out, err);
    if (src) CFRelease(src);
    return ok;
}

bool load_png_memory(const std::vector<uint8_t>& png, Image& out, std::string* err) {
    if (png.empty()) {
        if (err) *err = "empty png";
        return false;
    }

    CFDataRef data = CFDataCreate(kCFAllocatorDefault, png.data(),
                                  static_cast<CFIndex>(png.size()));
    if (!data) {
        if (err) *err = "cannot wrap png bytes";
        return false;
    }

    CGImageSourceRef src = CGImageSourceCreateWithData(data, nullptr);
    CFRelease(data);

    const bool ok = decode_source(src, out, err);
    if (src) CFRelease(src);
    return ok;
}
