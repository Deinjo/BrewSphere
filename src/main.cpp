/**
 * BrewSphere - fermentation status on the round GC9A01 display.
 */

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "hardware/display.h"
#include "services/brew_settings.h"
#include "services/display_settings.h"
#include "services/ota_update.h"
#include "services/radar_location.h"
#include "services/weather_time.h"
#include "services/wifi_setup.h"
#include "ui/brew_display.h"
#include "ui/status_screens.h"

namespace {

bool g_display_visible = false;
unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;

void showDisplayIfConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    g_display_visible = false;
    return;
  }
  services::weather::begin();
  ui::brewDisplayDraw();
  g_display_visible = true;
}

void onDisplayTap() {
  if (g_display_visible && WiFi.status() == WL_CONNECTED) {
    ui::brewDisplayDraw();
  }
}

void handleBootButton() {
  bootButtonPollLongPress();
  if (bootButtonConsumeTap()) {
    onDisplayTap();
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("BrewSphere");

  bootButtonInit();
  displayInit();
  statusScreenBrand();
  delay(1000);
  statusScreenBrandWordmark();
  delay(1000);
  ui::brewDisplayInit();
  if (wifiShowsSetupScreenOnBoot()) {
    statusScreenPortal();
  }
  services::location::init();
  services::settings::init();
  services::brew::init();
  services::weather::setPollFn(wifiLoop);

  if (wifiSetupConnect()) {
    showDisplayIfConnected();
  }
}

void loop() {
  handleBootButton();
  wifiLoop();
  if (g_display_visible) {
    ui::brewDisplayTick();
  }

  if (services::ota::inProgress()) {
    delay(10);
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (g_display_visible) {
      Serial.println("WiFi lost — will reconnect");
      g_display_visible = false;
    }

    if (g_wifi_down_since == 0) {
      g_wifi_down_since = millis();
    }

    const unsigned long down_ms = millis() - g_wifi_down_since;
    if (down_ms >= config::kWifiDownGraceMs &&
        millis() - g_last_reconnect_ms >= config::kWifiReconnectIntervalMs) {
      g_last_reconnect_ms = millis();
      if (wifiReconnect()) {
        g_wifi_down_since = 0;
        showDisplayIfConnected();
      }
    }
  } else {
    g_wifi_down_since = 0;
    if (!g_display_visible) {
      showDisplayIfConnected();
    } else if (services::weather::refreshIfDue(
                   services::location::lat(), services::location::lon())) {
      ui::brewDisplayRefresh();
    }
  }

  delay(10);
}
