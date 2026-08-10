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
constexpr uint16_t kBackground = 0x0043;  // RGB ~ 0, 8, 24
constexpr uint16_t kPanel = 0x08C5;       // RGB ~ 8, 54, 41
constexpr uint16_t kPanelEdge = 0x2A9F;   // muted steel blue
constexpr uint16_t kPanelShadow = 0x0064; // RGB ~ 0, 12, 33
constexpr uint16_t kBlue = 0x24BF;
constexpr uint16_t kCyan = 0x5DFF;
constexpr uint16_t kCream = 0xFFB0;
constexpr uint16_t kWhite = 0xFFFF;
constexpr uint16_t kMuted = 0x9D9F;

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
  (void)attenuation;
  drawArcSegment(140.0f, 260.0f, 108, 5.0f, kBlue);
  drawArcSegment(260.0f, 335.0f, 108, 5.0f, kCyan);
  drawArcSegment(335.0f, 400.0f, 108, 5.0f, kCream);

  constexpr float kDegToRad = 0.01745329252f;
  constexpr int kDotCount = 43;
  for (int i = 0; i < kDotCount; ++i) {
    const float ratio = static_cast<float>(i) / (kDotCount - 1);
    const float angle = (140.0f + 260.0f * ratio) * kDegToRad;
    const int x = kCenterX + static_cast<int>(std::lround(std::cos(angle) * 96));
    const int y = kCenterY + static_cast<int>(std::lround(std::sin(angle) * 96));
    const uint16_t color = ratio < 0.52f ? kBlue : kCream;
    s_draw->fillCircle(x, y, 2, color);
  }
}

void drawGaugeNeedle(float attenuation) {
  constexpr float kDegToRad = 0.01745329252f;
  const float ratio = clampPercent(attenuation) / 100.0f;
  const float angle = (140.0f + 260.0f * ratio) * kDegToRad;
  const int end_x = kCenterX + static_cast<int>(std::lround(std::cos(angle) * 92));
  const int end_y = kCenterY + static_cast<int>(std::lround(std::sin(angle) * 92));
  s_draw->drawWideLine(kCenterX, kCenterY, end_x, end_y, 1.5f, kCream);
  s_draw->fillCircle(kCenterX, kCenterY, 4, kCream);
  s_draw->fillCircle(kCenterX, kCenterY, 2, kBackground);
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
  // The reference keeps the main gauge open; only the lower batch card is a
  // filled panel.
  s_draw->fillTriangle(35, 166, 207, 166, 176, 216, kPanel);
  s_draw->fillTriangle(35, 166, 176, 216, 64, 216, kPanel);
  s_draw->drawWideLine(35, 166, 207, 166, 1.0f, kPanelEdge);
}

void drawBatchPanel(const services::weather::BrewData& data) {

  char batch[80] = {};
  if (data.batch_number > 0) {
    snprintf(batch, sizeof(batch), "BATCH #%d  %s", data.batch_number,
             data.batch_name[0] != '\0' ? data.batch_name : "-");
  } else {
    snprintf(batch, sizeof(batch), "BATCH  %s",
             data.batch_name[0] != '\0' ? data.batch_name : "WAITING");
  }
  setSmooth(0.52f);
  char fitted_batch[48] = {};
  fitText(fitted_batch, sizeof(fitted_batch), batch, 150);
  s_draw->setTextDatum(textdatum_t::middle_center);
  s_draw->setTextColor(kWhite, kPanel);
  s_draw->drawString(fitted_batch, kCenterX, 183);

  const char* recipe = data.recipe_name[0] != '\0' ? data.recipe_name : data.status;
  setSmooth(0.42f);
  char fitted_recipe[48] = {};
  fitText(fitted_recipe, sizeof(fitted_recipe), recipe, 150);
  s_draw->setTextColor(kMuted, kPanel);
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
  s_draw->fillCircle(kCenterX, kCenterY, 116, kBackground);
  s_draw->drawCircle(kCenterX, kCenterY, 116, 0x39D7);
  s_draw->drawCircle(kCenterX, kCenterY, 112, 0x12B4);

  drawGaugeRing(data.measured_attenuation_percent);
  drawGaugeNeedle(data.measured_attenuation_percent);
  drawInfoPanels();

  drawCentered("ATTENUATION", 27, kCream, 0.42f);
  char attenuation[20] = {};
  if (data.measured_attenuation_percent > 0.0f) {
    snprintf(attenuation, sizeof(attenuation), "%.0f%%",
             data.measured_attenuation_percent);
  } else {
    snprintf(attenuation, sizeof(attenuation), "--%%");
  }
  drawCentered(attenuation, 45, kCream, 0.58f);

  s_draw->drawWideLine(72, 67, 103, 67, 1.0f, kMuted);
  s_draw->drawWideLine(137, 67, 168, 67, 1.0f, kMuted);
  drawCentered("PLATO", 67, kWhite, 0.5f);
  char gravity[20] = {};
  if (data.specific_gravity > 0.0f) {
    snprintf(gravity, sizeof(gravity), "%.1f P",
             platoFromSg(data.specific_gravity));
  } else {
    snprintf(gravity, sizeof(gravity), "--.- P");
  }
  drawCentered(gravity, 91, kCream, 2.0f);
  char target[20] = {};
  if (data.estimated_final_gravity > 0.0f) {
    snprintf(target, sizeof(target), "ZIEL %.1f P",
             platoFromSg(data.estimated_final_gravity));
  } else {
    snprintf(target, sizeof(target), "ZIEL --.- P");
  }
  drawCentered(target, 112, kMuted, 0.40f);
  drawCentered(displayStatus(data.status), 126, kWhite, 0.52f);

  char temperature[32] = {};
  if (data.valid) {
    if (std::isfinite(data.fridge_temperature_c)) {
      snprintf(temperature, sizeof(temperature), "%.1f C   F %.1f C",
               data.temperature_c, data.fridge_temperature_c);
    } else {
      snprintf(temperature, sizeof(temperature), "%.1f C   F --",
               data.temperature_c);
    }
  } else {
    snprintf(temperature, sizeof(temperature), "--.- C   F --");
  }
  drawCentered(temperature, 148, kCyan, 0.62f);

  drawBatchPanel(data);

  char day[24] = {};
  if (data.brew_day > 0) {
    snprintf(day, sizeof(day), "TAG %d", data.brew_day);
  } else {
    snprintf(day, sizeof(day), "TAG --");
  }
  drawCentered(day, 229, kWhite, 0.52f);

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
      row[x * 3] = static_cast<uint8_t>((blue << 3) | (blue >> 2));
      row[x * 3 + 1] = static_cast<uint8_t>((green << 2) | (green >> 4));
      row[x * 3 + 2] = static_cast<uint8_t>((red << 3) | (red >> 2));
    }
    output.write(row, sizeof(row));
  }
}

}  // namespace ui
