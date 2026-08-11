#include "ui/brew_display.h"

#include <lgfx/v1/lgfx_fonts.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "config.h"
#include "hardware/display.h"
#include "hardware/display_font.h"
#include "services/weather_time.h"

namespace {

constexpr int kCenterX = 120;
constexpr int kCenterY = 120;

// LovyanGFX swaps red and blue for this panel when rgb_order is enabled.
constexpr uint16_t displayColor(uint8_t red, uint8_t green, uint8_t blue) {
  return static_cast<uint16_t>(
      ((((config::kDisplayRgbOrder ? blue : red) & 0xF8u) << 8) |
       ((green & 0xFCu) << 3) |
       ((config::kDisplayRgbOrder ? red : blue) >> 3)));
}

constexpr uint16_t kBackground = displayColor(0x0B, 0x35, 0x52);  // #0b3552
constexpr uint16_t kPanel = displayColor(0x17, 0x3F, 0x5D);       // #173f5d
constexpr uint16_t kPanelEdge = displayColor(0x62, 0x86, 0x9D);   // #62869d
constexpr uint16_t kBlue = displayColor(0x3D, 0x83, 0xDF);        // #3d83df
constexpr uint16_t kCyan = displayColor(0x41, 0xDE, 0xDE);        // #41dede
constexpr uint16_t kCream = displayColor(0xFF, 0xE3, 0x9B);      // #ffe39b
constexpr uint16_t kPlato = displayColor(0xFF, 0xF1, 0xC9);      // #fff1c9
constexpr uint16_t kLabel = displayColor(0xE8, 0xF0, 0xF5);       // #e8f0f5
constexpr uint16_t kMuted = displayColor(0x9B, 0xB1, 0xC2);       // #9bb1c2
constexpr uint16_t kBatchLabel = displayColor(0x7B, 0x96, 0xA8); // #7b96a8
constexpr uint16_t kBatchValue = displayColor(0xF0, 0xF4, 0xF6); // #f0f4f6
constexpr uint16_t kRecipe = displayColor(0xD5, 0xE0, 0xE7);     // #d5e0e7

static_assert(kBackground == (config::kDisplayRgbOrder ? 0x51A1 : 0x09AA));
static_assert(kBlue == (config::kDisplayRgbOrder ? 0xDC07 : 0x3C1B));
static_assert(kCyan == (config::kDisplayRgbOrder ? 0xDEE8 : 0x46FB));
static_assert(kCream == (config::kDisplayRgbOrder ? 0x9F1F : 0xFF13));

LGFX_Sprite s_frame(&tft);
bool s_frame_ready = false;
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

void setSmooth(float size) {
  if (displayFontIsSmooth()) {
    displayFontSetSmoothSize(*s_draw, size);
  } else {
    displayFontSetBitmap(*s_draw, &lgfx::v1::fonts::FreeSansBold12pt7b);
  }
}

void drawCentered(const char* text, int y, uint16_t color, float size) {
  setSmooth(size);
  s_draw->setTextDatum(textdatum_t::middle_center);
  s_draw->setTextColor(color, kBackground);
  s_draw->drawString(text, kCenterX, y);
}

void drawFixedTemperature(const char* label, const char* value, int label_x,
                          int value_x, int unit_x, int y) {
  setSmooth(0.62f);
  s_draw->setTextColor(kCyan, kBackground);
  s_draw->setTextDatum(textdatum_t::middle_left);
  s_draw->drawString(label, label_x, y);
  s_draw->setTextDatum(textdatum_t::middle_right);
  s_draw->drawString(value, value_x, y);
  s_draw->drawString("\xC2\xB0" "C", unit_x, y);
}

void drawArcSegment(float start_deg, float end_deg, int radius, float width,
                    uint16_t color) {
  constexpr float kDegToRad = 0.01745329252f;
  constexpr int kSteps = 34;
  int previous_x = 0;
  int previous_y = 0;
  for (int i = 0; i <= kSteps; ++i) {
    const float ratio = static_cast<float>(i) / kSteps;
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

void drawGaugeRing(float attenuation) {
  drawArcSegment(145.0f, 395.0f, 110, 8.0f, kCyan);
  const float ratio = clampPercent(attenuation) / 100.0f;
  if (ratio > 0.0f) {
    drawArcSegment(145.0f, 145.0f + 250.0f * ratio, 110, 8.0f, kBlue);
  }

  constexpr float kDegToRad = 0.01745329252f;
  constexpr int kDotCount = 41;
  const float color_break_ratio =
      clampPercent(services::weather::data().measured_attenuation_percent) / 100.0f;
  for (int i = 0; i < kDotCount; ++i) {
    const float ratio = static_cast<float>(i) / (kDotCount - 1);
    const float percent = ratio * 100.0f;
    const float angle = (145.0f + 250.0f * ratio) * kDegToRad;
    const int x = kCenterX + static_cast<int>(std::lround(std::cos(angle) * 97));
    const int y = kCenterY + static_cast<int>(std::lround(std::sin(angle) * 97));
    const uint16_t color = ratio < color_break_ratio ? kBlue : kCream;
    const int radius = std::fmod(percent, 10.0f) < 0.01f ? 3 : 2;
    s_draw->fillCircle(x, y, radius, color);
  }
}

void fitText(char* output, size_t output_len, const char* input, int max_width) {
  if (output_len == 0) {
    return;
  }
  snprintf(output, output_len, "%s", input != nullptr ? input : "");
  if (s_draw->textWidth(output) <= max_width) {
    return;
  }

  const size_t input_len = strlen(output);
  for (size_t length = input_len; length > 0; --length) {
    snprintf(output, output_len, "%.*s...", static_cast<int>(length), input);
    if (s_draw->textWidth(output) <= max_width) {
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

void drawInfoPanels() {
  s_draw->fillTriangle(34, 195, 57, 177, 64, 174, kPanel);
  s_draw->fillTriangle(34, 195, 64, 174, 176, 174, kPanel);
  s_draw->fillTriangle(34, 195, 176, 174, 183, 177, kPanel);
  s_draw->fillTriangle(34, 195, 183, 177, 206, 195, kPanel);
  s_draw->fillTriangle(34, 195, 206, 195, 177, 216, kPanel);
  s_draw->fillTriangle(34, 195, 177, 216, 63, 216, kPanel);
  s_draw->drawWideLine(57, 177, 64, 174, 1.0f, kPanelEdge);
  s_draw->drawWideLine(64, 174, 176, 174, 1.0f, kPanelEdge);
  s_draw->drawWideLine(176, 174, 183, 177, 1.0f, kPanelEdge);
  s_draw->drawWideLine(57, 177, 183, 177, 1.0f, kPanelEdge);
  s_draw->drawWideLine(34, 195, 57, 177, 1.0f, kPanelEdge);
  s_draw->drawWideLine(183, 177, 206, 195, 1.0f, kPanelEdge);
  s_draw->drawWideLine(34, 195, 63, 216, 1.0f, kPanelEdge);
  s_draw->drawWideLine(63, 216, 177, 216, 1.0f, kPanelEdge);
  s_draw->drawWideLine(177, 216, 206, 195, 1.0f, kPanelEdge);
}

void drawBatchPanel(const services::weather::BrewData& data) {

  char batch[64] = {};
  if (data.batch_number > 0) {
    snprintf(batch, sizeof(batch), "#%d  %s", data.batch_number,
             data.batch_name[0] != '\0' ? data.batch_name : "-");
  } else {
    snprintf(batch, sizeof(batch), "%s",
             data.batch_name[0] != '\0' ? data.batch_name : "WAITING");
  }
  setSmooth(0.52f);
  char fitted_batch[48] = {};
  fitText(fitted_batch, sizeof(fitted_batch), batch, 105);
  s_draw->setTextDatum(textdatum_t::middle_right);
   s_draw->setTextColor(kBatchLabel, kPanel);
   s_draw->drawString("BATCH:", 112, 190);
   s_draw->setTextDatum(textdatum_t::middle_left);
   s_draw->setTextColor(kBatchValue, kPanel);
   s_draw->drawString(fitted_batch, 116, 190);

  const char* recipe = data.recipe_name[0] != '\0' ? data.recipe_name : data.status;
  setSmooth(0.42f);
  char fitted_recipe[48] = {};
  fitText(fitted_recipe, sizeof(fitted_recipe), recipe, 150);
   s_draw->setTextColor(kRecipe, kPanel);
  s_draw->drawString(fitted_recipe[0] != '\0' ? fitted_recipe : "BREWFATHER",
                     kCenterX, 202);
}

}  // namespace

namespace ui {

void brewDisplayDraw() {
  const services::weather::BrewData& data = services::weather::data();
  if (!s_frame_ready) {
    s_frame.setColorDepth(16);
    s_frame.createSprite(240, 240);
    displayFontEnsureLoaded(s_frame);
    s_frame_ready = true;
  }
  s_draw = &s_frame;

  s_draw->fillScreen(kBackground);

  drawGaugeRing(data.measured_attenuation_percent);
  drawInfoPanels();

  char attenuation[20] = {};
  if (data.measured_attenuation_percent > 0.0f) {
    snprintf(attenuation, sizeof(attenuation), "%.0f%%",
             data.measured_attenuation_percent);
  } else {
    snprintf(attenuation, sizeof(attenuation), "--%%");
  }
  char attenuation_label[32] = {};
  snprintf(attenuation_label, sizeof(attenuation_label), "VERGAERGRAD %s",
           attenuation);
  drawCentered(attenuation_label, 27, kCream, 0.42f);

  s_draw->drawWideLine(77, 67, 97, 67, 1.0f, kMuted);
  s_draw->drawWideLine(143, 67, 163, 67, 1.0f, kMuted);
   drawCentered("PLATO", 67, kLabel, 0.5f);
  char gravity[20] = {};
  if (data.specific_gravity > 0.0f) {
    snprintf(gravity, sizeof(gravity), "%.1f \xC2\xB0P",
             platoFromSg(data.specific_gravity));
  } else {
    snprintf(gravity, sizeof(gravity), "--.- \xC2\xB0P");
  }
    drawCentered(gravity, 105, kPlato, 2.0f);
  char target[20] = {};
  if (data.estimated_final_gravity > 0.0f) {
    snprintf(target, sizeof(target), "ZIEL %.1f P",
             platoFromSg(data.estimated_final_gravity));
  } else {
    snprintf(target, sizeof(target), "ZIEL --.- P");
  }
   drawCentered(target, 119, kMuted, 0.40f);
   drawCentered(displayStatus(data.status), 135, kPlato, 0.52f);

  char target_temperature[20] = {};
  char fridge_temperature[20] = {};
  if (data.valid) {
     snprintf(target_temperature, sizeof(target_temperature), "%.1f",
              data.target_temperature_c);
    if (std::isfinite(data.fridge_temperature_c)) {
       snprintf(fridge_temperature, sizeof(fridge_temperature), "%.1f",
               data.fridge_temperature_c);
    } else {
      snprintf(fridge_temperature, sizeof(fridge_temperature), "--.-");
    }
  } else {
    snprintf(target_temperature, sizeof(target_temperature), "--.-");
    snprintf(fridge_temperature, sizeof(fridge_temperature), "--.-");
  }
  drawFixedTemperature("S:", target_temperature, 42, 87, 105, 157);
  drawFixedTemperature("I:", fridge_temperature, 135, 180, 197, 157);

  drawBatchPanel(data);

  char day[24] = {};
  if (data.brew_day > 0) {
    snprintf(day, sizeof(day), "TAG: %d", data.brew_day);
  } else {
    snprintf(day, sizeof(day), "TAG: --");
  }
  setSmooth(0.60f);
  s_draw->setTextDatum(textdatum_t::middle_right);
   s_draw->setTextColor(kBatchLabel, kBackground);
  s_draw->drawString("TAG:", 112, 229);
  s_draw->setTextDatum(textdatum_t::middle_left);
   s_draw->setTextColor(kRecipe, kBackground);
  s_draw->drawString(data.brew_day > 0 ? day + 5 : "--", 116, 229);

  s_frame.pushSprite(0, 0);
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
      uint8_t red = static_cast<uint8_t>((pixel >> 11) & 0x1F);
      const uint8_t green = static_cast<uint8_t>((pixel >> 5) & 0x3F);
      uint8_t blue = static_cast<uint8_t>(pixel & 0x1F);
      // Sprite pixels use the panel-compensated RGB565 values. Restore the
      // logical RGB order when exporting a browser-visible BMP.
      if (config::kDisplayRgbOrder) {
        std::swap(red, blue);
      }
      row[x * 3] = static_cast<uint8_t>((blue << 3) | (blue >> 2));
      row[x * 3 + 1] = static_cast<uint8_t>((green << 2) | (green >> 4));
      row[x * 3 + 2] = static_cast<uint8_t>((red << 3) | (red >> 2));
    }
    output.write(row, sizeof(row));
  }
}

}  // namespace ui
