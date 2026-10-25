#include "image.h"

#include <algorithm>
#include <cstring>
#include <fstream>

Image Image::from_screencap(const std::vector<uint8_t>& raw, std::string* err) {
    auto fail = [&](const std::string& why) {
        if (err) *err = why;
        return Image{};
    };

    if (raw.size() < 12) return fail("screencap header truncated");

    uint32_t w = 0, h = 0, fmt = 0;
    std::memcpy(&w, &raw[0], 4);
    std::memcpy(&h, &raw[4], 4);
    std::memcpy(&fmt, &raw[8], 4);

    if (w == 0 || h == 0 || w > 8192 || h > 8192) return fail("screencap bogus size");
    if (fmt != 1 && fmt != 3) return fail("screencap unexpected format " + std::to_string(fmt));

    const size_t need = static_cast<size_t>(w) * h * 4;
    if (raw.size() < 12 + need) return fail("screencap payload truncated");

    Image img(static_cast<int>(w), static_cast<int>(h));
    std::memcpy(img.px_.data(), raw.data() + 12, need);
    return img;
}

// ---------------- 最小 PNG 写入器 ----------------

namespace {

uint32_t crc_table[256];
bool crc_ready = false;

void crc_init() {
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
    crc_ready = true;
}

uint32_t crc32_of(const uint8_t* data, size_t len) {
    if (!crc_ready) crc_init();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) c = crc_table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

uint32_t adler32_of(const uint8_t* data, size_t len) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; ++i) {
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

void put_be32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x >> 24)); v.push_back(uint8_t(x >> 16));
    v.push_back(uint8_t(x >> 8));  v.push_back(uint8_t(x));
}

void write_chunk(std::vector<uint8_t>& out, const char* type,
                 const std::vector<uint8_t>& data) {
    put_be32(out, static_cast<uint32_t>(data.size()));
    size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    put_be32(out, crc32_of(out.data() + start, out.size() - start));
}

}  // namespace

bool Image::save_png(const std::string& path) const {
    if (px_.empty()) return false;

    // 原始扫描线：每行 1 字节 filter(0) + RGBA
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(h_) * (1 + static_cast<size_t>(w_) * 4));
    for (int y = 0; y < h_; ++y) {
        raw.push_back(0);
        const uint8_t* row = &px_[static_cast<size_t>(y) * w_ * 4];
        raw.insert(raw.end(), row, row + static_cast<size_t>(w_) * 4);
    }

    // zlib 流：stored deflate 块（不压缩，实现简单且无依赖）
    std::vector<uint8_t> z;
    z.push_back(0x78);  // CMF
    z.push_back(0x01);  // FLG (level 0, 32K window)
    size_t off = 0;
    while (off < raw.size()) {
        size_t n = std::min<size_t>(65535, raw.size() - off);
        bool final_block = (off + n == raw.size());
        z.push_back(final_block ? 0x01 : 0x00);
        z.push_back(uint8_t(n & 0xFF));
        z.push_back(uint8_t(n >> 8));
        z.push_back(uint8_t(~n & 0xFF));
        z.push_back(uint8_t((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + off, raw.begin() + off + n);
        off += n;
    }
    put_be32(z, adler32_of(raw.data(), raw.size()));

    std::vector<uint8_t> ihdr;
    put_be32(ihdr, static_cast<uint32_t>(w_));
    put_be32(ihdr, static_cast<uint32_t>(h_));
    ihdr.push_back(8);   // bit depth
    ihdr.push_back(6);   // colour type RGBA
    ihdr.push_back(0);   // compression
    ihdr.push_back(0);   // filter
    ihdr.push_back(0);   // interlace

    std::vector<uint8_t> file = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    write_chunk(file, "IHDR", ihdr);
    write_chunk(file, "IDAT", z);
    write_chunk(file, "IEND", {});

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(file.data()),
              static_cast<std::streamsize>(file.size()));
    return out.good();
}
