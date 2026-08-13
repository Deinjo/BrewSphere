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
#include "services/brew_settings.h"
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
bool s_refresh_requested = false;
brew::SourceMode s_last_source_mode = brew::SourceMode::kBrewfatherApi;

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
  const brew::BrewfatherCredentials& stored = brew::brewfatherCredentials();
  char credentials[sizeof(stored.user_id) + sizeof(stored.api_key) + 2] = {};
  snprintf(credentials, sizeof(credentials), "%s:%s", stored.user_id,
           stored.api_key);

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

bool getJson(const String& url, const String& authorization, JsonDocument& doc,
             const JsonDocument* filter = nullptr) {
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

  pollNetwork();
  const int code = http.GET();
  pollNetwork();
  if (code != HTTP_CODE_OK) {
    Serial.printf("brewfather: HTTP %d\n", code);
    http.end();
    return false;
  }

  const DeserializationError error =
      filter != nullptr
          ? deserializeJson(doc, http.getStream(),
                            DeserializationOption::Filter(
                                filter->as<JsonVariantConst>()))
          : deserializeJson(doc, http.getStream());
  http.end();
  pollNetwork();
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

bool copyMarkedDisplayName(const JsonVariantConst& value, char* target,
                           size_t target_len) {
  if (target_len == 0) {
    return false;
  }
  const char* source = value | "";
  const char* marker = strrchr(source, '#');
  if (marker == nullptr || marker[1] == '\0') {
    return false;
  }
  source = marker + 1;
  while (*source == ' ' || *source == '\t') {
    ++source;
  }
  if (*source == '\0') {
    return false;
  }
  const char* end = strchr(source, ')');
  const size_t source_length = end == nullptr
                                   ? strlen(source)
                                   : static_cast<size_t>(end - source);
  snprintf(target, target_len, "%.*s", static_cast<int>(source_length), source);
  size_t length = strlen(target);
  while (length > 0 && (target[length - 1] == ' ' || target[length - 1] == '\t')) {
    target[--length] = '\0';
  }
  return target[0] != '\0';
}

void copyDisplayName(const JsonVariantConst& value, char* target,
                     size_t target_len) {
  if (copyMarkedDisplayName(value, target, target_len)) {
    return;
  }
  if (target_len == 0) {
    return;
  }
  snprintf(target, target_len, "%s", value | "");
}

float specificGravityFromPlato(float plato) {
  float specific_gravity =
      1.0f + plato / (258.6f - (plato / 258.2f) * 227.1f);
  for (int iteration = 0; iteration < 4; ++iteration) {
    const float calculated =
        -616.868f + 1111.14f * specific_gravity -
        630.272f * specific_gravity * specific_gravity +
        135.997f * specific_gravity * specific_gravity * specific_gravity;
    const float derivative =
        1111.14f - 1260.544f * specific_gravity +
        407.991f * specific_gravity * specific_gravity;
    specific_gravity -= (calculated - plato) / derivative;
  }
  return specific_gravity;
}

bool applySimulatedValues() {
  const brew::SimulatedValues& simulated = brew::simulatedValues();
  float current_plato = simulated.plato;
  float attenuation = simulated.attenuation_percent;
  float target_temperature = simulated.target_temperature_c;
  float fridge_temperature = simulated.fridge_temperature_c;
  static bool demo_was_active = false;
  static unsigned long demo_started_ms = 0;
  if (simulated.demo_mode) {
    constexpr float kTwoPi = 6.283185307f;
    constexpr unsigned long kDemoCycleMs = 180000UL;
    if (!demo_was_active) {
      demo_started_ms = millis();
      demo_was_active = true;
    }
    const float phase =
        static_cast<float>((millis() - demo_started_ms) % kDemoCycleMs) /
                        static_cast<float>(kDemoCycleMs);
    const float progress = 0.5f - 0.5f * std::cos(kTwoPi * phase);
    current_plato = simulated.plato +
                    (simulated.target_plato - simulated.plato) * progress;
    attenuation = simulated.attenuation_percent +
                  (simulated.end_attenuation_percent -
                   simulated.attenuation_percent) * progress;
    target_temperature += 0.15f * std::sin(kTwoPi * phase);
    fridge_temperature += 0.35f * std::sin(kTwoPi * phase + 0.9f);
  } else {
    demo_was_active = false;
  }

  BrewData next_data;
  next_data.valid = true;
  snprintf(next_data.batch_id, sizeof(next_data.batch_id), "simulated");
  snprintf(next_data.batch_name, sizeof(next_data.batch_name), "%s",
           simulated.batch_name);
  snprintf(next_data.recipe_name, sizeof(next_data.recipe_name), "%s",
           simulated.recipe_name);
  snprintf(next_data.status, sizeof(next_data.status), "%s", simulated.status);
  next_data.batch_number = simulated.batch_number;
  next_data.brew_day = simulated.brew_day;
  next_data.temperature_c = fridge_temperature;
  next_data.target_temperature_c = target_temperature;
  next_data.fridge_temperature_c = fridge_temperature;
  next_data.specific_gravity = specificGravityFromPlato(current_plato);
  next_data.original_gravity = specificGravityFromPlato(simulated.plato);
  next_data.estimated_final_gravity =
      specificGravityFromPlato(simulated.target_plato);
  next_data.measured_final_gravity = next_data.specific_gravity;
  next_data.measured_attenuation_percent = attenuation;
  next_data.end_attenuation_percent = simulated.end_attenuation_percent;

  s_data = next_data;
  s_temperature_c = next_data.temperature_c;
  s_fridge_temperature_c = next_data.fridge_temperature_c;
  s_valid = true;
  static unsigned long last_demo_log_ms = 0;
  if (!simulated.demo_mode || millis() - last_demo_log_ms >= 10000UL) {
    Serial.printf(
        "brew simulation%s: %.1f P, target %.1f P, attenuation %.0f/%.0f%%\n",
        simulated.demo_mode ? " demo" : "", current_plato,
        simulated.target_plato, attenuation,
        simulated.end_attenuation_percent);
    last_demo_log_ms = millis();
  }
  return true;
}

bool fetch(double, double) {
  const brew::BrewfatherCredentials& credentials =
      brew::brewfatherCredentials();
  if (credentials.user_id[0] == '\0' || credentials.api_key[0] == '\0') {
    Serial.println("brewfather: credentials are not configured");
    return false;
  }

  const String authorization = basicAuthHeader();
  if (authorization.isEmpty()) {
    Serial.println("brewfather: could not create authorization header");
    return false;
  }

  JsonDocument batches;
  JsonDocument batches_filter;
  batches_filter[0]["_id"] = true;
  String batches_url = config::kBrewfatherApiBase;
  batches_url +=
      "/batches?status=Fermenting&limit=1&order_by=brewDate&order_by_direction=desc";
  if (!getJson(batches_url, authorization, batches, &batches_filter)) {
    return false;
  }

  JsonArray batch_list = batches.as<JsonArray>();
  if (batch_list.isNull() || batch_list.size() == 0 ||
      !batch_list[0]["_id"].is<const char*>()) {
    Serial.println("brewfather: no fermenting batch found");
    return false;
  }

  const String batch_id = batch_list[0]["_id"] | "";
  if (batch_id.isEmpty()) {
    Serial.println("brewfather: batch has no id");
    return false;
  }

  // Release the list before opening the next TLS connection.
  batches.clear();

  String batch_url = config::kBrewfatherApiBase;
  batch_url += "/batches/";
  batch_url += batch_id;
  batch_url +=
      "?include=estimatedFg,measuredOg,measuredFg,measuredAttenuation";

  JsonDocument batch;
  JsonDocument batch_filter;
  batch_filter["_id"] = true;
  batch_filter["description"] = true;
  batch_filter["name"] = true;
  batch_filter["recipe"]["name"] = true;
  batch_filter["status"] = true;
  batch_filter["batchNo"] = true;
  batch_filter["measuredOg"] = true;
  batch_filter["estimatedFg"] = true;
  batch_filter["measuredFg"] = true;
  batch_filter["measuredAttenuation"] = true;
  batch_filter["brewDate"] = true;
  batch_filter["events"][0]["description"] = true;
  if (!getJson(batch_url, authorization, batch, &batch_filter)) {
    return false;
  }

  BrewData next_data;
  next_data.valid = true;
  copyJsonString(batch["_id"], next_data.batch_id,
                sizeof(next_data.batch_id));
  if (!copyMarkedDisplayName(batch["description"], next_data.batch_name,
                             sizeof(next_data.batch_name))) {
    JsonArrayConst events = batch["events"].as<JsonArrayConst>();
    for (JsonObjectConst event : events) {
      if (copyMarkedDisplayName(event["description"], next_data.batch_name,
                                sizeof(next_data.batch_name))) {
        break;
      }
    }
  }
  if (next_data.batch_name[0] == '\0') {
    copyDisplayName(batch["name"], next_data.batch_name,
                    sizeof(next_data.batch_name));
  }
  copyJsonString(batch["recipe"]["name"], next_data.recipe_name,
                sizeof(next_data.recipe_name));
  copyJsonString(batch["status"], next_data.status, sizeof(next_data.status));
  next_data.batch_number = batch["batchNo"] | 0;
  next_data.original_gravity = batch["measuredOg"] | 0.0f;
  next_data.estimated_final_gravity = batch["estimatedFg"] | 0.0f;
  next_data.measured_final_gravity = batch["measuredFg"] | 0.0f;
  next_data.measured_attenuation_percent =
      batch["measuredAttenuation"] | 0.0f;
  if (next_data.original_gravity > 1.0f &&
      next_data.estimated_final_gravity > 0.0f) {
    const float end_attenuation =
        (next_data.original_gravity - next_data.estimated_final_gravity) /
        (next_data.original_gravity - 1.0f) * 100.0f;
    next_data.end_attenuation_percent =
        std::isfinite(end_attenuation) && end_attenuation >= 0.0f &&
                end_attenuation <= 100.0f
            ? end_attenuation
            : next_data.measured_attenuation_percent;
  } else {
    next_data.end_attenuation_percent =
        next_data.measured_attenuation_percent;
  }

  const uint64_t brew_date_ms = batch["brewDate"] | static_cast<uint64_t>(0);
  if (brew_date_ms > 0 && clockValid()) {
    const int64_t elapsed_seconds =
        static_cast<int64_t>(time(nullptr)) -
        static_cast<int64_t>(brew_date_ms / 1000ULL);
    if (elapsed_seconds >= 0) {
      next_data.brew_day = static_cast<int>(elapsed_seconds / 86400) + 1;
    }
  }

  // Keep only the compact data model while fetching the reading.
  batch.clear();

  String reading_url = config::kBrewfatherApiBase;
  reading_url += "/batches/";
  reading_url += batch_id;
  reading_url += "/readings/last";

  JsonDocument reading;
  JsonDocument reading_filter;
  reading_filter["temp"] = true;
  reading_filter["fridgeTemp"] = true;
  reading_filter["temp_target"] = true;
  reading_filter["sg"] = true;
  reading_filter["time"] = true;
  reading_filter["type"] = true;
  if (!getJson(reading_url, authorization, reading, &reading_filter)) {
    return false;
  }

  if (!reading["temp"].is<float>()) {
    Serial.println("brewfather: reading has no numeric temp field");
    return false;
  }
  if (!reading["sg"].is<float>()) {
    Serial.println("brewfather: reading has no numeric sg field");
    return false;
  }

  s_temperature_c = reading["temp"].as<float>();
  s_fridge_temperature_c = reading["fridgeTemp"].is<float>()
                               ? reading["fridgeTemp"].as<float>()
                               : NAN;
  next_data.temperature_c = s_temperature_c;
  next_data.target_temperature_c = reading["temp_target"].is<float>()
                                        ? reading["temp_target"].as<float>()
                                        : NAN;
  next_data.fridge_temperature_c = s_fridge_temperature_c;
  next_data.specific_gravity = reading["sg"].as<float>();
  next_data.reading_time_ms = reading["time"] | static_cast<uint64_t>(0);
  s_data = next_data;
  s_valid = true;
  Serial.printf("brewfather: temp %.1f C, fridge %.1f C, sg %.3f, sensor %s\n",
                s_temperature_c, s_fridge_temperature_c,
                next_data.specific_gravity,
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

void requestRefresh() { s_refresh_requested = true; }

bool refreshIfDue(double latitude, double longitude, bool force) {
  begin();
  const unsigned long now = millis();
  const brew::SourceMode source_mode = brew::sourceMode();
  const bool simulation_demo =
      source_mode == brew::SourceMode::kSimulated &&
      brew::simulatedValues().demo_mode;
  const unsigned long refresh_interval_ms =
      simulation_demo ? 1000UL : config::kBrewfatherFetchIntervalMs;
  const bool source_changed = source_mode != s_last_source_mode;
  const bool location_changed =
      fabs(latitude - s_last_latitude) > 0.0001 ||
      fabs(longitude - s_last_longitude) > 0.0001;
  if (!force && !s_refresh_requested && !source_changed && !location_changed &&
      s_last_attempt_ms != 0 &&
      now - s_last_attempt_ms < refresh_interval_ms) {
    return false;
  }
  s_refresh_requested = false;
  s_last_source_mode = source_mode;
  s_last_attempt_ms = now;
  s_last_latitude = latitude;
  s_last_longitude = longitude;
  if (source_mode == brew::SourceMode::kSimulated) {
    return applySimulatedValues();
  }
  if (fetch(latitude, longitude)) {
    return true;
  }
  s_data = BrewData{};
  s_temperature_c = 0.0f;
  s_fridge_temperature_c = NAN;
  s_valid = false;
  return true;
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
