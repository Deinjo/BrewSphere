#pragma once

#include <cstddef>
#include <cstdint>

namespace ui::assets {

struct Glyph {
  uint32_t codepoint;
  uint32_t bitmap_offset;
  uint16_t width;
  uint16_t height;
  int16_t x_offset;
  int16_t y_offset;
  int16_t advance_x64;
};

struct Font {
  const Glyph* glyphs;
  const uint8_t* bitmap;
  uint16_t glyph_count;
};

extern const Font kNotoRegular6;
extern const Font kNotoRegular12;
extern const Font kNotoRegular15;
extern const Font kNotoBold13;
extern const Font kNotoBold35;

extern const uint16_t kBackgroundRuns[];
extern const size_t kBackgroundRunWordCount;

constexpr int kAttenuationMaskX = 35;
constexpr int kAttenuationMaskY = 25;
constexpr int kAttenuationMaskWidth = 170;
constexpr int kAttenuationMaskHeight = 70;

extern const uint8_t kAttenuationMasks[];
extern const uint32_t kAttenuationMaskOffsets[102];

}  // namespace ui::assets
