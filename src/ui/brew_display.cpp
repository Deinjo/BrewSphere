#include "ui/brew_display.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <pgmspace.h>

#include "config.h"
#include "hardware/display.h"
#include "services/weather_time.h"
#include "ui/brew_assets.h"

namespace {

constexpr int kCenterX = 120;
constexpr int kCenterY = 120;

constexpr uint16_t displayColor(uint8_t red, uint8_t green, uint8_t blue) {
  return static_cast<uint16_t>((((red & 0xF8u) << 8) |
                                ((green & 0xFCu) << 3) | (blue >> 3)));
}

constexpr uint16_t kBackground = displayColor(0x0B, 0x35, 0x52);  // #0b3552
constexpr uint16_t kPanel = displayColor(0x17, 0x3F, 0x5D);       // #173f5d
constexpr uint16_t kBlue = displayColor(0xFF, 0x9D, 0x00);        // #ff9d00
constexpr uint16_t kCyan = displayColor(0x41, 0xDE, 0xDE);        // #41dede
constexpr uint16_t kCream = displayColor(0xFF, 0xE3, 0x9B);      // #ffe39b
constexpr uint16_t kPlato = displayColor(0xFF, 0xF1, 0xC9);      // #fff1c9
constexpr uint16_t kLabel = displayColor(0xE8, 0xF0, 0xF5);       // #e8f0f5
constexpr uint16_t kMuted = displayColor(0x9B, 0xB1, 0xC2);       // #9bb1c2
constexpr uint16_t kBatchLabel = displayColor(0x7B, 0x96, 0xA8); // #7b96a8
constexpr uint16_t kBatchValue = displayColor(0xF0, 0xF4, 0xF6); // #f0f4f6
constexpr uint16_t kRecipe = displayColor(0xD5, 0xE0, 0xE7);     // #d5e0e7

static_assert(kBackground == 0x09AA);
static_assert(kBlue == 0xFCE0);
static_assert(kCyan == 0x46FB);
static_assert(kCream == 0xFF13);

LGFX_Sprite s_frame(&tft);
bool s_frame_ready = false;
bool s_frame_alloc_attempted = false;
lgfx::LGFXBase* s_draw = &tft;

float clampPercent(float value) {
  return std::max(0.0f, std::min(100.0f, value));
}

float platoFromSg(float specific_gravity) {
  // Standard cubic approximation for SG values around normal wort gravity.
  return -616.868f + 1111.14f * specific_gravity -
         630.272f * specific_gravity * specific_gravity +
         135.997f * specific_gravity * specific_gravity * specific_gravity;
}

uint16_t displayPixel(uint16_t color) {
  return color;
}

uint16_t blend565(uint16_t foreground, uint16_t background, uint8_t alpha) {
  const uint8_t inverse = 15 - alpha;
  const uint16_t red = static_cast<uint16_t>(
      ((((foreground >> 11) & 0x1F) * alpha +
        ((background >> 11) & 0x1F) * inverse + 7) /
       15)
      << 11);
  const uint16_t green = static_cast<uint16_t>(
      ((((foreground >> 5) & 0x3F) * alpha +
        ((background >> 5) & 0x3F) * inverse + 7) /
       15)
      << 5);
  const uint16_t blue = static_cast<uint16_t>(
      (((foreground & 0x1F) * alpha + (background & 0x1F) * inverse + 7) /
       15));
  return red | green | blue;
}

uint32_t nextCodepoint(const char*& text) {
  const uint8_t first = static_cast<uint8_t>(*text++);
  if (first < 0x80) {
    return first;
  }
  const auto continuation = [](char value) {
    return (static_cast<uint8_t>(value) & 0xC0) == 0x80;
  };
  if ((first & 0xE0) == 0xC0) {
    if (*text == '\0' || !continuation(*text)) {
      return '?';
    }
    const uint8_t second = static_cast<uint8_t>(*text++);
    return ((first & 0x1F) << 6) | (second & 0x3F);
  }
  if ((first & 0xF0) == 0xE0) {
    if (text[0] == '\0' || text[1] == '\0' || !continuation(text[0]) ||
        !continuation(text[1])) {
      return '?';
    }
    const uint8_t second = static_cast<uint8_t>(*text++);
    const uint8_t third = static_cast<uint8_t>(*text++);
    return ((first & 0x0F) << 12) | ((second & 0x3F) << 6) | (third & 0x3F);
  }
  if ((first & 0xF8) == 0xF0 && text[0] != '\0' && text[1] != '\0' &&
      text[2] != '\0' && continuation(text[0]) && continuation(text[1]) &&
      continuation(text[2])) {
    text += 3;
  }
  return '?';
}

bool findGlyph(const ui::assets::Font& font, uint32_t codepoint,
               ui::assets::Glyph& result) {
  for (uint16_t index = 0; index < font.glyph_count; ++index) {
    memcpy_P(&result, font.glyphs + index, sizeof(result));
    if (result.codepoint == codepoint) {
      return true;
    }
  }
  if (codepoint != '?') {
    return findGlyph(font, '?', result);
  }
  return false;
}

int textWidthX64(const ui::assets::Font& font, const char* text) {
  int width = 0;
  while (text != nullptr && *text != '\0') {
    ui::assets::Glyph glyph{};
    if (findGlyph(font, nextCodepoint(text), glyph)) {
      width += glyph.advance_x64;
    }
  }
  return width;
}

int textWidth(const ui::assets::Font& font, const char* text) {
  return (textWidthX64(font, text) + 32) / 64;
}

enum class TextAnchor { kLeft, kMiddle, kRight };

void drawText(const ui::assets::Font& font, const char* text, int x,
              int baseline_y, uint16_t foreground, uint16_t background,
              TextAnchor anchor = TextAnchor::kLeft) {
  int cursor_x64 = x * 64;
  const int width_x64 = textWidthX64(font, text);
  if (anchor == TextAnchor::kMiddle) {
    cursor_x64 -= width_x64 / 2;
  } else if (anchor == TextAnchor::kRight) {
    cursor_x64 -= width_x64;
  }

  while (text != nullptr && *text != '\0') {
    ui::assets::Glyph glyph{};
    if (!findGlyph(font, nextCodepoint(text), glyph)) {
      continue;
    }
    const int glyph_x = (cursor_x64 + 32) / 64 + glyph.x_offset;
    const int glyph_y = baseline_y + glyph.y_offset;
    const int pixel_count = glyph.width * glyph.height;
    for (int pixel = 0; pixel < pixel_count; ++pixel) {
      const uint8_t packed = pgm_read_byte(
          font.bitmap + glyph.bitmap_offset + static_cast<uint32_t>(pixel / 2));
      const uint8_t alpha = pixel & 1 ? packed & 0x0F : packed >> 4;
      if (alpha == 0) {
        continue;
      }
      const int pixel_x = glyph_x + pixel % glyph.width;
      const int pixel_y = glyph_y + pixel / glyph.width;
      s_draw->drawPixel(pixel_x, pixel_y,
                        alpha == 15 ? foreground
                                    : blend565(foreground, background, alpha));
    }
    cursor_x64 += glyph.advance_x64;
  }
}

void drawStaticBackground() {
  int pixel = 0;
  for (size_t index = 0; index < ui::assets::kBackgroundRunWordCount;
       index += 2) {
    int count = pgm_read_word(ui::assets::kBackgroundRuns + index);
    const uint16_t color = displayPixel(
        pgm_read_word(ui::assets::kBackgroundRuns + index + 1));
    while (count > 0) {
      const int x = pixel % 240;
      const int length = std::min(count, 240 - x);
      s_draw->drawFastHLine(x, pixel / 240, length, color);
      pixel += length;
      count -= length;
    }
  }
}

void drawAttenuationLabel(float attenuation) {
  const int percent = static_cast<int>(std::lround(clampPercent(attenuation)));
  uint32_t offset = pgm_read_dword(ui::assets::kAttenuationMaskOffsets + percent);
  const uint32_t end =
      pgm_read_dword(ui::assets::kAttenuationMaskOffsets + percent + 1);
  int pixel = 0;
  while (offset < end) {
    const int count = pgm_read_byte(ui::assets::kAttenuationMasks + offset++);
    const uint8_t alpha =
        pgm_read_byte(ui::assets::kAttenuationMasks + offset++);
    if (alpha != 0) {
      for (int index = 0; index < count; ++index) {
        const int position = pixel + index;
        const int x = ui::assets::kAttenuationMaskX +
                      position % ui::assets::kAttenuationMaskWidth;
        const int y = ui::assets::kAttenuationMaskY +
                      position / ui::assets::kAttenuationMaskWidth;
        const uint16_t background = s_frame_ready
                                        ? static_cast<uint16_t>(s_frame.readPixel(x, y))
                                        : kBackground;
        s_draw->drawPixel(x, y, alpha == 15
                                   ? kCream
                                   : blend565(kCream, background, alpha));
      }
    }
    pixel += count;
  }
}

void drawArcSegment(float start_deg, float end_deg, int radius, float width,
                    uint16_t color) {
  constexpr float kDegToRad = 0.01745329252f;
  const int steps = std::max(
      1, static_cast<int>(std::ceil(std::abs(end_deg - start_deg))));
  int previous_x = 0;
  int previous_y = 0;
  for (int i = 0; i <= steps; ++i) {
    const float ratio = static_cast<float>(i) / steps;
    const float angle = (start_deg + (end_deg - start_deg) * ratio) * kDegToRad;
    const int x = kCenterX + static_cast<int>(std::lround(std::cos(angle) * radius));
    const int y = kCenterY + static_cast<int>(std::lround(std::sin(angle) * radius));
    if (i > 0) {
      s_draw->drawWideLine(previous_x, previous_y, x, y, width, color);
    }
    previous_x = x;
    previous_y = y;
  }
}

void drawGaugeRing(float attenuation, float end_attenuation) {
  const float ratio = clampPercent(attenuation) / 100.0f;
  if (ratio > 0.0f) {
    const float end_angle = 145.0f + 250.0f * ratio;
    // LovyanGFX drawWideLine uses this value as the line radius. A radius of
    // four matches the SVG background arc's eight-pixel stroke width.
    drawArcSegment(145.0f, end_angle, 110, 4.0f, kBlue);
    constexpr float kDegToRad = 0.01745329252f;
    const int start_x = kCenterX + static_cast<int>(
        std::lround(std::cos(145.0f * kDegToRad) * 110));
    const int start_y = kCenterY + static_cast<int>(
        std::lround(std::sin(145.0f * kDegToRad) * 110));
    const int end_x = kCenterX + static_cast<int>(
        std::lround(std::cos(end_angle * kDegToRad) * 110));
    const int end_y = kCenterY + static_cast<int>(
        std::lround(std::sin(end_angle * kDegToRad) * 110));
    s_draw->fillCircle(start_x, start_y, 4, kBlue);
    s_draw->fillCircle(end_x, end_y, 4, kBlue);
  }

  constexpr float kDegToRad = 0.01745329252f;
  constexpr int kDotCount = 41;
  const float color_break_ratio = clampPercent(end_attenuation) / 100.0f;
  for (int i = 0; i < kDotCount; ++i) {
    const float ratio = static_cast<float>(i) / (kDotCount - 1);
    const float angle = (145.0f + 250.0f * ratio) * kDegToRad;
    const int x = kCenterX + static_cast<int>(std::lround(std::cos(angle) * 97));
    const int y = kCenterY + static_cast<int>(std::lround(std::sin(angle) * 97));
    const uint16_t color = ratio < color_break_ratio ? kBlue : kCream;
    // Four 2.5% intervals make one 10% major step, including the 0% dot.
    const int radius = i % 4 == 0 ? 3 : 2;
    s_draw->fillCircle(x, y, radius, color);
  }
}

void fitText(char* output, size_t output_len, const char* input, int max_width,
             const ui::assets::Font& font) {
  if (output_len == 0) {
    return;
  }
  snprintf(output, output_len, "%s", input != nullptr ? input : "");
  if (textWidth(font, output) <= max_width) {
    return;
  }

  size_t length = strlen(output);
  while (length > 0) {
    do {
      --length;
    } while (length > 0 &&
             (static_cast<uint8_t>(output[length]) & 0xC0) == 0x80);
    output[length] = '\0';
    char candidate[64] = {};
    snprintf(candidate, sizeof(candidate), "%s...", output);
    if (textWidth(font, candidate) <= max_width) {
      snprintf(output, output_len, "%s", candidate);
      return;
    }
  }
  snprintf(output, output_len, "...");
}

const char* displayStatus(const char* status) {
  if (status == nullptr || status[0] == '\0') {
    return "BREWSPHERE";
  }
  if (strcmp(status, "Fermenting") == 0) {
    return "GAERUNG";
  }
  if (strcmp(status, "Brewing") == 0) {
    return "BRAUTAG";
  }
  if (strcmp(status, "Conditioning") == 0) {
    return "REIFUNG";
  }
  return status;
}

void drawBatchPanel(const services::weather::BrewData& data) {

  char batch[64] = {};
  if (data.batch_number > 0) {
    snprintf(batch, sizeof(batch), " #%d %s", data.batch_number,
             data.batch_name[0] != '\0' ? data.batch_name : "Sud");
  } else {
    snprintf(batch, sizeof(batch), " %s",
             data.batch_name[0] != '\0' ? data.batch_name : "WAITING");
  }
  char fitted_batch[48] = {};
  fitText(fitted_batch, sizeof(fitted_batch), batch, 105,
          ui::assets::kNotoRegular12);
  const int batch_width = textWidth(ui::assets::kNotoRegular12, "BATCH:");
  const int value_width = textWidth(ui::assets::kNotoRegular12, fitted_batch);
  const int batch_x = kCenterX - (batch_width + value_width) / 2;
  drawText(ui::assets::kNotoRegular12, "BATCH:", batch_x, 190, kBatchLabel,
           kPanel);
  drawText(ui::assets::kNotoRegular12, fitted_batch, batch_x + batch_width, 190,
           kBatchValue, kPanel);

  const char* recipe = data.recipe_name[0] != '\0' ? data.recipe_name : data.status;
  char fitted_recipe[48] = {};
  fitText(fitted_recipe, sizeof(fitted_recipe), recipe, 150,
          ui::assets::kNotoRegular12);
  drawText(ui::assets::kNotoRegular12,
           fitted_recipe[0] != '\0' ? fitted_recipe : "BREWFATHER", kCenterX,
           206, kRecipe, kPanel, TextAnchor::kMiddle);
}

}  // namespace

namespace ui {

bool brewDisplayInit() {
  if (s_frame_ready || s_frame_alloc_attempted) {
    return s_frame_ready;
  }
  s_frame_alloc_attempted = true;
  s_frame.setColorDepth(16);
  if (!s_frame.createSprite(240, 240)) {
    Serial.printf(
        "brew display: frame sprite allocation failed; direct rendering, heap %u\n",
        ESP.getFreeHeap());
    return false;
  }
  s_frame_ready = true;
  Serial.printf("brew display: frame sprite ready, free heap %u\n",
                ESP.getFreeHeap());
  return true;
}

bool brewDisplayFrameAvailable() { return s_frame_ready; }

void brewDisplayDraw() {
  const services::weather::BrewData& data = services::weather::data();
  brewDisplayInit();
  s_draw = s_frame_ready ? static_cast<lgfx::LGFXBase*>(&s_frame)
                         : static_cast<lgfx::LGFXBase*>(&tft);

  drawStaticBackground();
  drawGaugeRing(data.measured_attenuation_percent,
                data.end_attenuation_percent);
  drawAttenuationLabel(data.measured_attenuation_percent);

  drawText(ui::assets::kNotoRegular12, "PLATO", kCenterX, 70, kLabel,
           kBackground, TextAnchor::kMiddle);
  char gravity[20] = {};
  if (data.specific_gravity > 0.0f) {
    snprintf(gravity, sizeof(gravity), "%.1f \xC2\xB0P",
             platoFromSg(data.specific_gravity));
  } else {
    snprintf(gravity, sizeof(gravity), "--.- \xC2\xB0P");
  }
  drawText(ui::assets::kNotoBold35, gravity, kCenterX, 105, kPlato,
           kBackground, TextAnchor::kMiddle);
  char target[20] = {};
  if (data.estimated_final_gravity > 0.0f) {
    snprintf(target, sizeof(target), "ZIEL %.1f \xC2\xB0P",
             platoFromSg(data.estimated_final_gravity));
  } else {
    snprintf(target, sizeof(target), "ZIEL --.- \xC2\xB0P");
  }
  drawText(ui::assets::kNotoRegular6, target, kCenterX, 119, kMuted,
           kBackground, TextAnchor::kMiddle);
  char status[32] = {};
  fitText(status, sizeof(status), displayStatus(data.status), 130,
          ui::assets::kNotoBold13);
  drawText(ui::assets::kNotoBold13, status, kCenterX, 135, kPlato,
           kBackground, TextAnchor::kMiddle);

  char target_temperature[20] = {};
  char fridge_temperature[20] = {};
  if (data.valid && std::isfinite(data.target_temperature_c)) {
    snprintf(target_temperature, sizeof(target_temperature), "%.1f",
             data.target_temperature_c);
  } else {
    snprintf(target_temperature, sizeof(target_temperature), "--.-");
  }
  if (data.valid) {
    if (std::isfinite(data.fridge_temperature_c)) {
      snprintf(fridge_temperature, sizeof(fridge_temperature), "%.1f",
               data.fridge_temperature_c);
    } else {
      snprintf(fridge_temperature, sizeof(fridge_temperature), "--.-");
    }
  } else {
    snprintf(fridge_temperature, sizeof(fridge_temperature), "--.-");
  }
  drawText(ui::assets::kNotoRegular15, "S:", 42, 157, kCyan, kBackground);
  drawText(ui::assets::kNotoRegular15, target_temperature, 87, 157, kCyan,
           kBackground, TextAnchor::kRight);
  drawText(ui::assets::kNotoRegular15, "\xC2\xB0" "C", 105, 157, kCyan,
           kBackground, TextAnchor::kRight);
  drawText(ui::assets::kNotoRegular15, "I:", 135, 157, kCyan, kBackground);
  drawText(ui::assets::kNotoRegular15, fridge_temperature, 180, 157, kCyan,
           kBackground, TextAnchor::kRight);
  drawText(ui::assets::kNotoRegular15, "\xC2\xB0" "C", 197, 157, kCyan,
           kBackground, TextAnchor::kRight);

  drawBatchPanel(data);

  char day[24] = {};
  if (data.brew_day > 0) {
    snprintf(day, sizeof(day), " %d", data.brew_day);
  } else {
    snprintf(day, sizeof(day), " --");
  }
  const int day_label_width = textWidth(ui::assets::kNotoRegular12, "TAG:");
  const int day_value_width = textWidth(ui::assets::kNotoRegular12, day);
  const int day_x = kCenterX - (day_label_width + day_value_width) / 2;
  drawText(ui::assets::kNotoRegular12, "TAG:", day_x, 229, kBatchLabel,
           kBackground);
  drawText(ui::assets::kNotoRegular12, day, day_x + day_label_width, 229,
           kRecipe, kBackground);

  if (s_frame_ready) {
    s_frame.pushSprite(0, 0);
  }
  s_draw = &tft;
}

void brewDisplayRefresh() { brewDisplayDraw(); }

void brewDisplayTick() {}

void brewDisplayWriteBmp(Print& output) {
  constexpr int kBmpWidth = 240;
  constexpr int kBmpHeight = 240;
  constexpr uint32_t kHeaderSize = 54;
  constexpr uint32_t kRowSize = kBmpWidth * 3;
  constexpr uint32_t kImageSize = kRowSize * kBmpHeight;
  constexpr uint32_t kFileSize = kHeaderSize + kImageSize;

  uint8_t header[kHeaderSize] = {};
  header[0] = 'B';
  header[1] = 'M';
  auto put32 = [&header](size_t offset, uint32_t value) {
    header[offset] = static_cast<uint8_t>(value);
    header[offset + 1] = static_cast<uint8_t>(value >> 8);
    header[offset + 2] = static_cast<uint8_t>(value >> 16);
    header[offset + 3] = static_cast<uint8_t>(value >> 24);
  };
  put32(2, kFileSize);
  put32(10, kHeaderSize);
  put32(14, 40);
  put32(18, kBmpWidth);
  put32(22, kBmpHeight);
  header[26] = 1;
  header[28] = 24;
  put32(34, kImageSize);
  output.write(header, sizeof(header));

  uint8_t row[kRowSize];
  for (int y = kBmpHeight - 1; y >= 0; --y) {
    for (int x = 0; x < kBmpWidth; ++x) {
      const uint16_t pixel = s_frame_ready ? s_frame.readPixel(x, y) : 0;
      const uint8_t red = static_cast<uint8_t>((pixel >> 11) & 0x1F);
      const uint8_t green = static_cast<uint8_t>((pixel >> 5) & 0x3F);
      const uint8_t blue = static_cast<uint8_t>(pixel & 0x1F);
      row[x * 3] = static_cast<uint8_t>((blue << 3) | (blue >> 2));
      row[x * 3 + 1] = static_cast<uint8_t>((green << 2) | (green >> 4));
      row[x * 3 + 2] = static_cast<uint8_t>((red << 3) | (red >> 2));
    }
    output.write(row, sizeof(row));
  }
}

}  // namespace ui
