#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace thps3_ap {
struct GameFont {
  struct Glyph {
    std::uint16_t x = 0, y = 0, width = 0, height = 0, baseline = 0;
  };
  std::array<Glyph, 256> glyphs{};
  std::vector<std::uint32_t> pixels;
  int width = 0, line_height = 15, baseline = 13;

  std::array<float, 4> Sample(const Glyph &glyph, float x, float y) const {
    // premultiply before filtering so transparent borders do not turn dark.
    const int left = static_cast<int>(std::floor(x));
    const int top = static_cast<int>(std::floor(y));
    const float fx = x - left, fy = y - top;
    std::array<float, 4> result{};
    for (int row = 0; row < 2; ++row)
      for (int col = 0; col < 2; ++col) {
        const int sx = left + col, sy = top + row;
        if (sx < 0 || sy < 0 || sx >= glyph.width || sy >= glyph.height)
          continue;
        const auto pixel = pixels[(glyph.y + sy) * width + glyph.x + sx];
        const float weight = (col ? fx : 1 - fx) * (row ? fy : 1 - fy);
        const float alpha = (pixel >> 24) / 255.0f * weight;
        result[0] += (pixel >> 16 & 255) * alpha;
        result[1] += (pixel >> 8 & 255) * alpha;
        result[2] += (pixel & 255) * alpha;
        result[3] += alpha;
      }
    return result;
  }

  bool Parse(std::span<const std::uint8_t> bytes) {
    *this = {};
    if (bytes.size() < 16)
      return false;
    const auto u16 = [&](std::size_t at) -> std::uint16_t {
      return bytes[at] | (bytes[at + 1] << 8);
    };
    const auto u32 = [&](std::size_t at) -> std::uint32_t {
      return u16(at) | (static_cast<std::uint32_t>(u16(at + 2)) << 16);
    };
    const auto count = u32(4), height = u32(8), base = u32(12);
    if (u32(0) != bytes.size() || !count || count > 256 || !height ||
        height > 128 || base > height)
      return false;
    const auto texture = 16 + count * 4;
    if (bytes.size() < texture + 16)
      return false;
    const auto w = u16(texture + 4), h = u16(texture + 6);
    if (!w || !h || w > 2048 || h > 2048 || u16(texture + 8) != 8 ||
        u32(texture) != bytes.size() - texture)
      return false;
    const auto image = texture + 16;
    const auto palette = image + static_cast<std::size_t>(w) * h;
    const auto rectangles = palette + 1024 + 4;
    if (rectangles + count * 8 != bytes.size() || u32(palette + 1024) != count)
      return false;
    GameFont parsed;
    parsed.width = w;
    parsed.line_height = height;
    parsed.baseline = base;
    for (std::size_t i = 0; i < count; ++i) {
      const auto code = u16(18 + i * 4);
      const auto at = rectangles + i * 8;
      const Glyph glyph{u16(at), u16(at + 2), u16(at + 4), u16(at + 6),
                        u16(16 + i * 4)};
      if (code >= parsed.glyphs.size() || parsed.glyphs[code].width ||
          !glyph.width || !glyph.height || glyph.x + glyph.width > w ||
          glyph.y + glyph.height > h || glyph.baseline > base ||
          base - glyph.baseline + glyph.height > height)
        return false;
      parsed.glyphs[code] = glyph;
    }
    parsed.pixels.reserve(static_cast<std::size_t>(w) * h);
    for (std::size_t i = image; i < palette; ++i) {
      const auto color = palette + bytes[i] * 4;
      parsed.pixels.push_back(
          (static_cast<std::uint32_t>(bytes[color + 3]) << 24) |
          (bytes[color] << 16) | (bytes[color + 1] << 8) | bytes[color + 2]);
    }
    *this = std::move(parsed);
    return true;
  }
};
} // namespace thps3_ap
