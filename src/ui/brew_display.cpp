#include "ui/brew_display.h"

#include <lgfx/v1/lgfx_fonts.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "hardware/display.h"
#include "hardware/display_font.h"
#include "services/weather_time.h"

namespace {

constexpr int kCenterX = 120;
constexpr int kCenterY = 120;
constexpr uint16_t kBackground = 0x0310;
constexpr uint16_t kPanel = 0x0B25;
constexpr uint16_t kPanelEdge = 0x2D5A;
constexpr uint16_t kBlue = 0x24BF;
constexpr uint16_t kCyan = 0x5DFF;
constexpr uint16_t kCream = 0xFFB0;
constexpr uint16_t kWhite = 0xFFFF;
constexpr uint16_t kMuted = 0x9D9F;

float clampPercent(float value) {
  return std::max(0.0f, std::min(100.0f, value));
}

void setSmooth(float size) {
  if (displayFontIsSmooth()) {
    displayFontSetSmoothSize(tft, size);
  } else {
    displayFontSetBitmap(tft, &lgfx::v1::fonts::FreeSansBold12pt7b);
  }
}

void drawCentered(const char* text, int y, uint16_t color, float size) {
  setSmooth(size);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setTextColor(color, kBackground);
  tft.drawString(text, kCenterX, y);
}

void drawGaugeRing(float attenuation) {
  constexpr int kDotCount = 54;
  constexpr float kStartDeg = 140.0f;
  constexpr float kEndDeg = 400.0f;
  constexpr float kDegToRad = 0.01745329252f;
  const float progress = clampPercent(attenuation) / 100.0f;

  for (int i = 0; i < kDotCount; ++i) {
    const float ratio = static_cast<float>(i) / (kDotCount - 1);
    const float angle = (kStartDeg + (kEndDeg - kStartDeg) * ratio) * kDegToRad;
    const int radius = 108;
    const int x = kCenterX + static_cast<int>(std::lround(std::cos(angle) * radius));
    const int y = kCenterY + static_cast<int>(std::lround(std::sin(angle) * radius));
    const bool active = ratio <= progress;
    tft.fillCircle(x, y, active ? 3 : 2, active ? (ratio < 0.72f ? kBlue : kCyan)
                                                   : 0x31B6);
  }
}

void drawGaugeTicks() {
  constexpr float kDegToRad = 0.01745329252f;
  for (int i = 0; i <= 10; ++i) {
    const float angle = (140.0f + i * 26.0f) * kDegToRad;
    const int x = kCenterX + static_cast<int>(std::lround(std::cos(angle) * 94));
    const int y = kCenterY + static_cast<int>(std::lround(std::sin(angle) * 94));
    tft.fillCircle(x, y, 1, kCream);
  }
}

void drawBatchPanel(const services::weather::BrewData& data) {
  tft.fillRoundRect(35, 168, 170, 48, 8, kPanel);
  tft.drawRoundRect(35, 168, 170, 48, 8, kPanelEdge);

  char batch[80] = {};
  if (data.batch_number > 0) {
    snprintf(batch, sizeof(batch), "BATCH #%d  %s", data.batch_number,
             data.batch_name[0] != '\0' ? data.batch_name : "-");
  } else {
    snprintf(batch, sizeof(batch), "BATCH  %s",
             data.batch_name[0] != '\0' ? data.batch_name : "WAITING");
  }
  setSmooth(0.52f);
  tft.setTextDatum(textdatum_t::middle_center);
  tft.setTextColor(kWhite, kPanel);
  tft.drawString(batch, kCenterX, 183);

  const char* recipe = data.recipe_name[0] != '\0' ? data.recipe_name : data.status;
  setSmooth(0.42f);
  tft.setTextColor(kMuted, kPanel);
  tft.drawString(recipe[0] != '\0' ? recipe : "BREWFATHER", kCenterX, 202);
}

}  // namespace

namespace ui {

void brewDisplayDraw() {
  const services::weather::BrewData& data = services::weather::data();
  tft.fillScreen(kBackground);
  tft.fillCircle(kCenterX, kCenterY, 116, kBackground);
  tft.drawCircle(kCenterX, kCenterY, 116, 0x39D7);
  tft.drawCircle(kCenterX, kCenterY, 112, 0x12B4);

  drawGaugeRing(data.measured_attenuation_percent);
  drawGaugeTicks();

  drawCentered("ATTENUATION", 27, kCream, 0.42f);
  char attenuation[20] = {};
  if (data.measured_attenuation_percent > 0.0f) {
    snprintf(attenuation, sizeof(attenuation), "%.0f%%",
             data.measured_attenuation_percent);
  } else {
    snprintf(attenuation, sizeof(attenuation), "--%%");
  }
  drawCentered(attenuation, 45, kCream, 0.58f);

  drawCentered("SG", 67, kWhite, 0.5f);
  char gravity[20] = {};
  if (data.specific_gravity > 0.0f) {
    snprintf(gravity, sizeof(gravity), "%.3f", data.specific_gravity);
  } else {
    snprintf(gravity, sizeof(gravity), "-.---");
  }
  drawCentered(gravity, 91, kCream, 1.48f);
  drawCentered(data.status[0] != '\0' ? data.status : "BREWSPHERE", 113,
               kWhite, 0.52f);

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
  drawCentered(temperature, 139, kCyan, 0.58f);

  drawBatchPanel(data);

  char day[24] = {};
  if (data.brew_day > 0) {
    snprintf(day, sizeof(day), "TAG %d", data.brew_day);
  } else {
    snprintf(day, sizeof(day), "TAG --");
  }
  drawCentered(day, 229, kWhite, 0.52f);
}

void brewDisplayRefresh() { brewDisplayDraw(); }

void brewDisplayTick() {}

}  // namespace ui
