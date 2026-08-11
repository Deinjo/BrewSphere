#pragma once

#include <cstddef>
#include <cstdint>

namespace services::weather {

using PollFn = void (*)();

struct BrewData {
  bool valid = false;
  char batch_id[32] = {};
  char batch_name[64] = {};
  char recipe_name[64] = {};
  char status[20] = {};
  int batch_number = 0;
  int brew_day = 0;
  float temperature_c = 0.0f;
  float target_temperature_c = 0.0f;
  float fridge_temperature_c = 0.0f;
  float specific_gravity = 0.0f;
  float original_gravity = 0.0f;
  float estimated_final_gravity = 0.0f;
  float measured_final_gravity = 0.0f;
  float measured_attenuation_percent = 0.0f;
  uint64_t reading_time_ms = 0;
};

/** Start UTC NTP synchronization. Safe to call after every reconnect. */
void begin();
void setPollFn(PollFn fn);

/**
 * Refresh current conditions and the location's UTC offset when due.
 * Returns true only when displayable data changed.
 */
bool refreshIfDue(double latitude, double longitude, bool force = false);

bool valid();
/** Latest Brewfather batch and reading data. Valid after a successful refresh. */
const BrewData& data();
/** Current local minute of day, or -1 while the clock is not valid. */
int localMinuteOfDay();
void formatWeatherLine(char* out, size_t out_len);
void formatDateTimeLine(char* out, size_t out_len);

}  // namespace services::weather
