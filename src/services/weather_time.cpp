#include "services/weather_time.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <mbedtls/base64.h>

#include <ArduinoJson.h>

#include <cmath>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <sys/time.h>

#include "config.h"
#include "services/display_settings.h"

namespace services::weather {
namespace {

constexpr time_t kMinimumValidEpoch = 1609459200;  // 2021-01-01 UTC

bool s_started = false;
bool s_valid = false;
float s_temperature_c = 0.0f;
float s_fridge_temperature_c = NAN;
BrewData s_data;
int32_t s_utc_offset_seconds = 0;
unsigned long s_last_attempt_ms = 0;
double s_last_latitude = 999.0;
double s_last_longitude = 999.0;
PollFn s_poll_fn = nullptr;

bool clockValid() { return time(nullptr) >= kMinimumValidEpoch; }

void pollNetwork() {
  if (s_poll_fn != nullptr) {
    s_poll_fn();
  }
}

int64_t daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
  const unsigned day_of_year =
      (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned day_of_era =
      year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
  return static_cast<int64_t>(era) * 146097 +
         static_cast<int64_t>(day_of_era) - 719468;
}

void seedClockFromApiTime(const char* local_iso_time, int32_t utc_offset) {
  if (clockValid() || local_iso_time == nullptr) {
    return;
  }

  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  if (sscanf(local_iso_time, "%d-%d-%dT%d:%d", &year, &month, &day, &hour,
             &minute) != 5) {
    return;
  }

  const time_t local_epoch = static_cast<time_t>(
      daysFromCivil(year, static_cast<unsigned>(month),
                    static_cast<unsigned>(day)) *
          86400 +
      hour * 3600 + minute * 60);
  if (local_epoch < kMinimumValidEpoch) {
    return;
  }

  timeval value = {};
  value.tv_sec = local_epoch - utc_offset;
  settimeofday(&value, nullptr);
}

String basicAuthHeader() {
  char credentials[sizeof(config::kBrewfatherUserId) +
                   sizeof(config::kBrewfatherApiKey) + 2] = {};
  snprintf(credentials, sizeof(credentials), "%s:%s", config::kBrewfatherUserId,
           config::kBrewfatherApiKey);

  unsigned char encoded[sizeof(credentials) * 2] = {};
  size_t encoded_len = 0;
  const int result = mbedtls_base64_encode(
      encoded, sizeof(encoded), &encoded_len,
      reinterpret_cast<const unsigned char*>(credentials),
      strlen(credentials));
  if (result != 0) {
    return String();
  }

  String header = "Basic ";
  header.concat(reinterpret_cast<const char*>(encoded), encoded_len);
  return header;
}

bool getJson(const String& url, const String& authorization, JsonDocument& doc) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("brewfather: http.begin failed");
    return false;
  }
  http.setConnectTimeout(config::kBrewfatherRequestTimeoutMs);
  http.setTimeout(config::kBrewfatherRequestTimeoutMs);
  http.addHeader("Authorization", authorization);

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("brewfather: HTTP %d\n", code);
    http.end();
    return false;
  }

  const DeserializationError error = deserializeJson(doc, http.getStream());
  http.end();
  if (error) {
    Serial.printf("brewfather: JSON parse error: %s\n", error.c_str());
    return false;
  }
  return true;
}

void copyJsonString(const JsonVariantConst& value, char* target,
                    size_t target_len) {
  if (target_len == 0) {
    return;
  }
  const char* source = value | "";
  snprintf(target, target_len, "%s", source);
}

bool fetch(double, double) {
  if (config::kBrewfatherUserId[0] == '\0' ||
      config::kBrewfatherApiKey[0] == '\0') {
    Serial.println("brewfather: credentials are not configured");
    return false;
  }

  const String authorization = basicAuthHeader();
  if (authorization.isEmpty()) {
    Serial.println("brewfather: could not create authorization header");
    return false;
  }

  JsonDocument batches;
  String batches_url = config::kBrewfatherApiBase;
  batches_url += "/batches?status=Fermenting&limit=50";
  if (!getJson(batches_url, authorization, batches)) {
    return false;
  }

  JsonArray batch_list = batches.as<JsonArray>();
  if (batch_list.isNull() || batch_list.size() == 0 ||
      !batch_list[0]["_id"].is<const char*>()) {
    Serial.println("brewfather: no fermenting batch found");
    return false;
  }

  const char* batch_id = batch_list[0]["_id"];

  String batch_url = config::kBrewfatherApiBase;
  batch_url += "/batches/";
  batch_url += batch_id;
  batch_url +=
      "?include=estimatedFg,measuredOg,measuredFg,measuredAttenuation";

  JsonDocument batch;
  if (!getJson(batch_url, authorization, batch)) {
    return false;
  }

  String reading_url = config::kBrewfatherApiBase;
  reading_url += "/batches/";
  reading_url += batch_id;
  reading_url += "/readings/last";

  JsonDocument reading;
  if (!getJson(reading_url, authorization, reading)) {
    return false;
  }

  if (!reading["temp"].is<float>()) {
    Serial.println("brewfather: reading has no numeric temp field");
    return false;
  }

  s_temperature_c = reading["temp"].as<float>();
  s_fridge_temperature_c = reading["fridgeTemp"].is<float>()
                               ? reading["fridgeTemp"].as<float>()
                               : NAN;

  BrewData next_data;
  next_data.valid = true;
  copyJsonString(batch["_id"], next_data.batch_id,
                sizeof(next_data.batch_id));
  copyJsonString(batch["name"], next_data.batch_name,
                sizeof(next_data.batch_name));
  copyJsonString(batch["recipe"]["name"], next_data.recipe_name,
                sizeof(next_data.recipe_name));
  copyJsonString(batch["status"], next_data.status, sizeof(next_data.status));
  next_data.batch_number = batch["batchNo"] | 0;
  next_data.temperature_c = s_temperature_c;
  next_data.fridge_temperature_c = s_fridge_temperature_c;
  next_data.specific_gravity = reading["sg"] | 0.0f;
  next_data.original_gravity = batch["measuredOg"] | 0.0f;
  next_data.estimated_final_gravity = batch["estimatedFg"] | 0.0f;
  next_data.measured_final_gravity = batch["measuredFg"] | 0.0f;
  next_data.measured_attenuation_percent =
      batch["measuredAttenuation"] | 0.0f;
  next_data.reading_time_ms = reading["time"] | static_cast<uint64_t>(0);

  const uint64_t brew_date_ms = batch["brewDate"] | static_cast<uint64_t>(0);
  if (brew_date_ms > 0 && clockValid()) {
    const int64_t elapsed_seconds =
        static_cast<int64_t>(time(nullptr)) -
        static_cast<int64_t>(brew_date_ms / 1000ULL);
    if (elapsed_seconds >= 0) {
      next_data.brew_day = static_cast<int>(elapsed_seconds / 86400) + 1;
    }
  }
  s_data = next_data;
  s_valid = true;
  Serial.printf("brewfather: temp %.1f C, fridge %.1f C, sensor %s\n",
                s_temperature_c, s_fridge_temperature_c,
                reading["type"] | "unknown");
  return true;
}

}  // namespace

void begin() {
  if (!s_started) {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    s_started = true;
  }
}

void setPollFn(PollFn fn) { s_poll_fn = fn; }

bool refreshIfDue(double latitude, double longitude, bool force) {
  begin();
  const unsigned long now = millis();
  const bool location_changed =
      fabs(latitude - s_last_latitude) > 0.0001 ||
      fabs(longitude - s_last_longitude) > 0.0001;
  if (!force && !location_changed && s_last_attempt_ms != 0 &&
       now - s_last_attempt_ms < config::kBrewfatherFetchIntervalMs) {
    return false;
  }
  s_last_attempt_ms = now;
  s_last_latitude = latitude;
  s_last_longitude = longitude;
  return fetch(latitude, longitude);
}

bool valid() { return s_valid; }

const BrewData& data() { return s_data; }

int localMinuteOfDay() {
  if (!clockValid()) {
    return -1;
  }
  const time_t local_epoch = time(nullptr) + s_utc_offset_seconds;
  struct tm local = {};
  gmtime_r(&local_epoch, &local);
  return local.tm_hour * 60 + local.tm_min;
}

void formatWeatherLine(char* out, size_t out_len) {
  if (out_len == 0) {
    return;
  }
  if (!s_valid) {
    snprintf(out, out_len, "BREW --");
    return;
  }

  float temperature = s_temperature_c;
  float fridge_temperature = s_fridge_temperature_c;
  const char unit = settings::temperatureFahrenheit() ? 'F' : 'C';
  if (settings::temperatureFahrenheit()) {
    temperature = temperature * 9.0f / 5.0f + 32.0f;
    if (std::isfinite(fridge_temperature)) {
      fridge_temperature = fridge_temperature * 9.0f / 5.0f + 32.0f;
    }
  }
  if (std::isfinite(fridge_temperature)) {
    snprintf(out, out_len, "T %.1f%c F %.1f%c", temperature, unit,
             fridge_temperature, unit);
  } else {
    snprintf(out, out_len, "T %.1f%c F --", temperature, unit);
  }
}

void formatDateTimeLine(char* out, size_t out_len) {
  if (out_len == 0) {
    return;
  }

  const time_t utc_now = time(nullptr);
  if (utc_now < kMinimumValidEpoch) {
    snprintf(out, out_len, "--:-- -- ---");
    return;
  }

  const time_t local_now = utc_now + s_utc_offset_seconds;
  tm local = {};
  gmtime_r(&local_now, &local);
  constexpr const char* kMonths[] = {"JAN", "FEB", "MAR", "APR",
                                     "MAY", "JUN", "JUL", "AUG",
                                     "SEP", "OCT", "NOV", "DEC"};
  const char* month =
      local.tm_mon >= 0 && local.tm_mon < 12 ? kMonths[local.tm_mon] : "---";

  if (settings::use24HourClock()) {
    snprintf(out, out_len, "%02d:%02d %02d %s", local.tm_hour, local.tm_min,
             local.tm_mday, month);
    return;
  }

  int hour = local.tm_hour % 12;
  if (hour == 0) {
    hour = 12;
  }
  snprintf(out, out_len, "%d:%02d%c %02d %s", hour, local.tm_min,
           local.tm_hour >= 12 ? 'P' : 'A', local.tm_mday, month);
}

}  // namespace services::weather
